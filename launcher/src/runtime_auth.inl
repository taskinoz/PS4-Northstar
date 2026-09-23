// Atlas authentication groundwork.
//
// **How Northstar auth actually works**, from the reference implementation:
//
//  1. The client exchanges its Origin token with the master server
//     (`/client/origin_auth?id=<uid>&token=<originToken>`) and gets back a
//     Northstar player token, kept as `m_sOwnClientAuthToken`.
//  2. To join a server it calls
//     `/client/auth_with_server?id=<uid>&playerToken=<token>&server=<id>&password=<pw>`
//     and gets back `{ip, port, authToken}` - a token minted for that one
//     connection.
//  3. It puts that token in the **`serverfilter` convar** and runs
//     `connect <ip>:<port>`. `serverfilter` is an ordinary userinfo convar that
//     Northstar repurposes, so the token rides along in the connect handshake.
//  4. The server receives `(uid, serverFilter)` as parameters of
//     `CBaseServer::ConnectClient` and calls `CheckAuthentication(uid, token)`,
//     which looks the token up and compares the uid Atlas recorded against the
//     uid the client sent. A mismatch is the `"Authentication Failed."` seen in
//     this project's captured server log, where the PS4 client arrived as
//     `uid 1`.
//
// So a genuine connection needs the client to present **both** halves: a token
// Atlas minted, and the uid Atlas minted it for. The plan of record is that a
// PC-side helper performs steps 1 and 2 - the EA credential never reaches the
// PS4 - and the console receives only the uid and the per-connection token.
//
// **What the probe below established.**
//
// `serverFilter` **does exist on this build** and is reachable. Searching the
// binaries for `serverfilter` found nothing, which briefly looked like the
// console build had dropped it; the registered name is camelCase
// (`"Only connects to matchmaking servers with the same value"`) and FindVar is
// case-insensitive, so the lookup succeeds. Writing it round-trips: it reads
// back exactly what was written, which proves the token half of the handshake
// can be driven from here without a server in the loop.
//
// `nucleus_pid` also exists and currently holds `"0"`, which is the shape of a
// convar holding a persona id. Whether writing it changes the uid the server
// receives is **not** established - the captured server log shows the client
// arriving as `uid 1`, not `0`, so the connect uid may come from somewhere else
// entirely. That needs a server to test.
//
// The ConVar layout for this build, from the same probe: +0x18 name, +0x20
// help, +0x40 default value, +0x48 current value.
constexpr const char* kAuthProbeConVars[] = {
    "platform_user_id",
    "mp_allowed",
    "nucleus_pid",
    "nucleus_hostname",
    "serverfilter",
    "name",
    "ns_auth_allow_insecure",
    "ns_has_agreed_to_send_token",
    "sv_cheats",
};
constexpr std::size_t kAuthConVarScanBytes = 0x60;

bool AuthAddressReadable(const void* address) noexcept {
    if (!address) return false;
    OrbisKernelVirtualQueryInfo info{};
    const auto page = reinterpret_cast<std::uintptr_t>(address) & ~std::uintptr_t(0x3fff);
    if (sceKernelVirtualQuery(reinterpret_cast<const void*>(page), 0, &info, sizeof(info)) != 0)
        return false;
    return info.isCommitted != 0;
}

// A pointer is only followed when its page is mapped, and only the first 48
// bytes are read, so a non-string pointer produces a short unreadable line
// rather than a fault.
void LogAuthConVarStrings(const char* name, void* convar) noexcept {
    const auto base = reinterpret_cast<std::uintptr_t>(convar);
    for (std::size_t offset = 0; offset < kAuthConVarScanBytes; offset += sizeof(void*)) {
        const auto slot = reinterpret_cast<std::uintptr_t>(convar) + offset;
        if (!AuthAddressReadable(reinterpret_cast<const void*>(slot))) continue;
        const auto value = *reinterpret_cast<std::uintptr_t*>(slot);
        if (value == 0) continue;
        const char* text = reinterpret_cast<const char*>(value);
        if (!AuthAddressReadable(text)) continue;
        char preview[49]{};
        std::size_t length = 0;
        while (length < sizeof(preview) - 1) {
            const char c = text[length];
            if (c == '\0') break;
            if (c < 0x20 || c > 0x7e) { length = 0; break; }
            preview[length] = c;
            ++length;
        }
        if (length == 0) continue;
        preview[length] = '\0';
        LogFormat("[NorthstarPS4] auth convar %s +0x%02zx -> \"%s\"\n", name, offset, preview);
    }
    (void)base;
}

// `ConVar::SetValue(const char*)` is **vtable slot 15**. Slots 15 to 18 are the
// IConVar forwarders: each loads the parent convar from `this + 0x38` and tail
// calls the parent's own vtable at +0x98, +0xa0, +0xa8 and +0xb0, which are
// slots 19 to 22. Disassembling those four settles which is which - slot 19
// (`engine+0x2057f0`) keeps `rsi` as a pointer, slot 21 keeps `esi` as an int -
// so the order is the usual `const char*`, `float`, `int`, `Color`. Calling
// slot 15 rather than 19 directly keeps the parent redirect intact, which
// matters for any convar that has one.
constexpr int kConVarSetStringSlot = 15;
constexpr std::uintptr_t kConVarSetStringThunkVa = 0x206040;
constexpr std::uintptr_t kConVarSetStringImplVa = 0x2057f0;
// This build's ConVar layout, from the probe below: +0x18 name, +0x20 help,
// +0x40 default value, +0x48 current value.
constexpr std::size_t kConVarValueOffset = 0x48;

// Proven once and left off: writing the auth convar on every boot is noise in
// a log that a later connect problem would be read from.
constexpr bool kAuthConVarRoundTripTest = false;

using ConVarSetStringFn = void (*)(void*, const char*);
bool g_authConVarWriteReady = false;

// Refuses unless both the forwarder and the implementation match, so a
// different build never reaches a guessed slot with a string argument.
void ValidateConVarWriter(std::uintptr_t engineBase, std::size_t engineSize) noexcept {
    constexpr std::uint8_t thunkPreimage[] = {
        0x48, 0x8b, 0x7f, 0x38, 0x48, 0x8b, 0x07, 0x48,
        0x8b, 0x80, 0x98, 0x00, 0x00, 0x00, 0xff, 0xe0};
    constexpr std::uint8_t implPreimage[] = {
        0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56,
        0x41, 0x54, 0x53, 0x48, 0x83, 0xec, 0x50, 0x4c};
    g_authConVarWriteReady =
        ValidateEnginePreimage(engineBase, engineSize, kConVarSetStringThunkVa,
            thunkPreimage, sizeof(thunkPreimage)) &&
        ValidateEnginePreimage(engineBase, engineSize, kConVarSetStringImplVa,
            implPreimage, sizeof(implPreimage));
    LogFormat("[NorthstarPS4] auth convar writer ready=%d\n", g_authConVarWriteReady ? 1 : 0);
}

const char* ReadConVarValue(void* convar) noexcept {
    if (!AuthAddressReadable(convar)) return nullptr;
    const auto slot = reinterpret_cast<std::uintptr_t>(convar) + kConVarValueOffset;
    if (!AuthAddressReadable(reinterpret_cast<const void*>(slot))) return nullptr;
    const char* value = *reinterpret_cast<const char**>(slot);
    return AuthAddressReadable(value) ? value : nullptr;
}

bool SetConVarString(void* convar, const char* value) noexcept {
    if (!g_authConVarWriteReady || !AuthAddressReadable(convar) || !value) return false;
    auto** vtable = *reinterpret_cast<void***>(convar);
    if (!AuthAddressReadable(vtable) || !AuthAddressReadable(&vtable[kConVarSetStringSlot]))
        return false;
    auto setValue = reinterpret_cast<ConVarSetStringFn>(vtable[kConVarSetStringSlot]);
    setValue(convar, value);
    return true;
}

// The vtable is dumped as engine-relative offsets so further slots can be
// identified offline by disassembly, never by calling a guessed one.
void LogAuthConVarVtable(const char* name, void* convar,
    std::uintptr_t engineBase, std::size_t engineSize) noexcept {
    if (!AuthAddressReadable(convar)) return;
    auto** vtable = *reinterpret_cast<void***>(convar);
    if (!AuthAddressReadable(vtable)) return;
    for (int i = 0; i < 24; ++i) {
        if (!AuthAddressReadable(&vtable[i])) break;
        const auto entry = reinterpret_cast<std::uintptr_t>(vtable[i]);
        if (entry == 0) continue;
        if (entry >= engineBase && entry < engineBase + engineSize)
            LogFormat("[NorthstarPS4] auth %s vtable[%d] = engine+%lx\n", name, i, entry - engineBase);
        else
            LogFormat("[NorthstarPS4] auth %s vtable[%d] = %p (outside engine)\n", name, i,
                reinterpret_cast<void*>(entry));
    }
}

void ProbeAuthConVars(void* cvar, ModFindVarFn findVar,
    std::uintptr_t engineBase, std::size_t engineSize) noexcept {
    if (!cvar || !findVar) return;
    void* serverFilter = nullptr;
    for (const char* name : kAuthProbeConVars) {
        void* found = findVar(cvar, name);
        LogFormat("[NorthstarPS4] auth convar %-28s = %p\n", name, found);
        if (found) LogAuthConVarStrings(name, found);
        if (found && !serverFilter && !std::strcmp(name, "serverfilter")) serverFilter = found;
    }
    if (serverFilter) LogAuthConVarVtable("serverFilter", serverFilter, engineBase, engineSize);

    // Round-trip the convar that carries the auth token. This proves the
    // client half of the handshake can be driven from here without needing a
    // server: if the readback matches, setting a real Atlas token is the same
    // call with a different string. The value is restored afterwards.
    ValidateConVarWriter(engineBase, engineSize);
    if (kAuthConVarRoundTripTest && serverFilter && g_authConVarWriteReady) {
        const char* before = ReadConVarValue(serverFilter);
        LogFormat("[NorthstarPS4] auth serverFilter before = \"%s\"\n", before ? before : "(null)");
        static char restore[128]{};
        if (before) std::snprintf(restore, sizeof(restore), "%s", before);
        constexpr const char* kMarker = "NorthstarPS4AuthRoundTrip";
        if (SetConVarString(serverFilter, kMarker)) {
            const char* after = ReadConVarValue(serverFilter);
            const bool matched = after && !std::strcmp(after, kMarker);
            LogFormat("[NorthstarPS4] auth serverFilter write %s (read back \"%s\")\n",
                matched ? "SUCCEEDED" : "FAILED", after ? after : "(null)");
            SetConVarString(serverFilter, restore);
        }
    }
}

// Applying an exported Atlas identity.
//
// `platform_user_id` - "Platform user id (origin user id on PC, xuid on
// xboxone)" - is the uid half, and it is an ordinary writable convar holding
// "0" on this platform. `Export-AtlasCredentials.ps1` writes the uid and player
// token from a signed-in PC client to the file below; when it is present the
// uid is applied at startup.
//
// Nothing happens without that file, so a profile with no exported identity
// behaves exactly as before.
//
// **Unverified:** whether the engine actually reads this convar when building
// the connect handshake. The captured server log shows the PS4 arriving as
// `uid 1` while the convar reads "0", so either it is not the source or it is
// sampled somewhere else. That needs a server to settle, and until it is
// settled this only changes a convar.
constexpr const char* kAtlasIdentityFile = "/data/northstar_ps4/atlas_identity.json";
constexpr std::size_t kAtlasIdentityMax = 4096;

// Identity state, read once at startup and reported to script.
enum class AtlasIdentityState { Missing, Incomplete, Malformed, Ready };
AtlasIdentityState g_atlasIdentityState = AtlasIdentityState::Missing;
char g_atlasUid[32]{};
char g_atlasToken[40]{};

// Shown in the UI when authentication is unavailable, so the message has to say
// what to actually do rather than just that it failed.
const char* AtlasIdentityMessage() noexcept {
    switch (g_atlasIdentityState) {
    case AtlasIdentityState::Ready:
        return "Signed in with an imported Atlas identity";
    case AtlasIdentityState::Incomplete:
        return "atlas_identity.json has no playerToken. Re-export it from a PC running Northstar and signed in";
    case AtlasIdentityState::Malformed:
        return "atlas_identity.json could not be read. Re-export it with scripts/Export-AtlasCredentials.ps1";
    default:
        break;
    }
    return "No Atlas identity. On a PC running Northstar and signed in, run scripts/Export-AtlasCredentials.ps1 "
           "and copy atlas_identity.json into /data/northstar_ps4/";
}
const char* AtlasIdentityCode() noexcept {
    switch (g_atlasIdentityState) {
    case AtlasIdentityState::Ready: return "PS4_AUTH_IMPORTED";
    case AtlasIdentityState::Incomplete: return "PS4_AUTH_NO_TOKEN";
    case AtlasIdentityState::Malformed: return "PS4_AUTH_BAD_IDENTITY";
    default: break;
    }
    return "PS4_AUTH_NO_IDENTITY";
}
bool AtlasIdentityReady() noexcept { return g_atlasIdentityState == AtlasIdentityState::Ready; }

struct AtlasIdentityVisitor {
    int depth = 0;
    std::string pending;
    std::string uid;
    std::string token;
    bool OnObjectBegin() { ++depth; return true; }
    bool OnObjectEnd() { --depth; pending.clear(); return true; }
    bool OnArrayBegin() { ++depth; return true; }
    bool OnArrayEnd() { --depth; return true; }
    bool OnKey(const char* key, std::size_t size) {
        pending.assign(key, size);
        return true;
    }
    bool OnString(const char* text, std::size_t size) {
        if (depth == 1 && pending == "uid") uid.assign(text, size);
        else if (depth == 1 && pending == "playerToken") token.assign(text, size);
        pending.clear();
        return true;
    }
    bool OnBool(bool) { pending.clear(); return true; }
    bool OnNull() { pending.clear(); return true; }
    bool OnInteger(int) { pending.clear(); return true; }
    bool OnFloat(float) { pending.clear(); return true; }
};

void ApplyAtlasIdentity(void* cvar, ModFindVarFn findVar) noexcept {
    static char text[kAtlasIdentityMax];
    std::size_t size = 0;
    if (!ReadFileIntoBuffer(kAtlasIdentityFile, text, sizeof(text), size)) {
        // Left at Missing. Logged once so the guidance is in the log as well as
        // in the menu, because this is the state most people will hit first.
        LogFormat("[NorthstarPS4] atlas identity: %s\n", AtlasIdentityMessage());
        return;
    }

    AtlasIdentityVisitor visitor;
    if (!JsonParse(text, visitor).ok || visitor.uid.empty()) {
        g_atlasIdentityState = AtlasIdentityState::Malformed;
        LogFormat("[NorthstarPS4] atlas identity: %s\n", AtlasIdentityMessage());
        return;
    }
    // Digits only: this goes into a convar the engine may parse as an integer.
    for (const char c : visitor.uid) {
        if (c < '0' || c > '9') {
            g_atlasIdentityState = AtlasIdentityState::Malformed;
            LogFormat("[NorthstarPS4] atlas identity: uid is not numeric\n");
            return;
        }
    }
    // The token is what actually authenticates; a uid on its own identifies the
    // account but proves nothing, so it is reported as incomplete rather than
    // being treated as a session.
    bool tokenUsable = visitor.token.size() == 32;
    for (const char c : visitor.token) {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) { tokenUsable = false; break; }
    }
    if (visitor.token.empty()) {
        g_atlasIdentityState = AtlasIdentityState::Incomplete;
    } else if (!tokenUsable) {
        g_atlasIdentityState = AtlasIdentityState::Malformed;
        LogFormat("[NorthstarPS4] atlas identity: playerToken is not 32 hex characters\n");
    } else {
        g_atlasIdentityState = AtlasIdentityState::Ready;
        std::snprintf(g_atlasToken, sizeof(g_atlasToken), "%s", visitor.token.c_str());
    }
    std::snprintf(g_atlasUid, sizeof(g_atlasUid), "%s", visitor.uid.c_str());
    LogFormat("[NorthstarPS4] atlas identity state=%s (%s)\n",
        AtlasIdentityCode(), AtlasIdentityMessage());
    if (!cvar || !findVar || !g_authConVarWriteReady) {
        LogFormat("[NorthstarPS4] atlas identity: uid not applied, convar writing unavailable\n");
        return;
    }
    void* platformUserId = findVar(cvar, "platform_user_id");
    if (!platformUserId) {
        LogFormat("[NorthstarPS4] atlas identity: platform_user_id not found\n");
        return;
    }
    // Copied, not held: the pointer aliases the convar's own storage, so
    // reading it after the write reports the new value and the log line claims
    // the uid was already what we just set it to.
    char before[64]{};
    if (const char* current = ReadConVarValue(platformUserId))
        std::snprintf(before, sizeof(before), "%s", current);
    if (!SetConVarString(platformUserId, visitor.uid.c_str())) {
        LogFormat("[NorthstarPS4] atlas identity: platform_user_id write refused\n");
        return;
    }
    const char* after = ReadConVarValue(platformUserId);
    LogFormat("[NorthstarPS4] atlas identity applied uid \"%s\" -> \"%s\"\n",
        before, after ? after : "(null)");
    // The player token is deliberately not logged, and not applied here: the
    // value `serverFilter` needs is the per-connection token Atlas mints for one
    // server, not the long-lived player token.
}

// Unlocking the multiplayer button.
//
// `ui/panel_mainmenu.nut` threads `UpdatePlayButton( file.fdButton )` onto the
// "Launch Northstar" button, and on this platform that compiles the
// `#elseif PS4_PROG` branch - the console permission chain. It ends with
//
//     isLocked = file.mpButtonActivateFunc == null
//     Hud_SetLocked( button, isLocked )
//
// so any branch that nulls the activate function leaves the button locked and
// clicking it does nothing. One of those branches is
// `!hasPermission || !isMPAllowed`, where `IsStryderAllowingMP()` is just
// `GetConVarInt( "mp_allowed" ) == 1`. This build boots with `mp_allowed` at
// `-1`, the value meaning Stryder has not answered - and it never will, because
// there is no Stryder session here.
//
// Northstar already refuses to care about Stryder on PC: its own
// `IsStryderAuthenticated()` returns `true` unconditionally in the non-vanilla
// build, precisely because the answer is irrelevant when not using official
// servers. The PS4 script still consults it only because the console branch is
// the one that compiles. Setting the convar to 1 puts this build in the same
// position PC is already in.
//
// This is not the only branch that can lock the button - the chain also checks
// `Console_IsOnline`, `HasLatestPatch`, `Ps4_PSN_Is_Loggedin`,
// `Console_HasPermissionToPlayMultiplayer` and an age check, and those are
// natives rather than convars. The message the menu shows under the button
// names whichever branch fired, so it identifies the next one if this is not
// enough on its own.
void AllowMultiplayerMenu(void* cvar, ModFindVarFn findVar) noexcept {
    if (!cvar || !findVar || !g_authConVarWriteReady) return;
    void* allowed = findVar(cvar, "mp_allowed");
    if (!allowed) {
        LogFormat("[NorthstarPS4] mp_allowed not found; multiplayer button left as-is\n");
        return;
    }
    char before[32]{};
    if (const char* current = ReadConVarValue(allowed))
        std::snprintf(before, sizeof(before), "%s", current);
    if (!std::strcmp(before, "1")) return;
    if (!SetConVarString(allowed, "1")) {
        LogFormat("[NorthstarPS4] mp_allowed write refused\n");
        return;
    }
    const char* after = ReadConVarValue(allowed);
    LogFormat("[NorthstarPS4] mp_allowed \"%s\" -> \"%s\" (IsStryderAllowingMP)\n",
        before, after ? after : "(null)");
}
