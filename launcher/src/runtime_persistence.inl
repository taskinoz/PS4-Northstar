// Server-side persistence availability for a locally hosted lobby.
//
// With the lobby finally reaching script, the first thing it does is write a
// persistent var, and the SERVER VM stops on:
//
//   SCRIPT ERROR: [SERVER] Persistent data not available for client #0.
//    -> player.SetPersistentVar( "privateMatchState", 0 ) // disable spectator
//   Lobby_OnClientConnectionStarted()        lobby/_lobby.gnut line [24]
//   CodeCallback_OnClientConnectionStarted() mp/_base_gametype_mp.gnut line [79]
//
// On PC this never happens, because NorthstarLauncher marks persistence ready
// natively before any script runs. `ServerAuthenticationManager::Authenticate-
// Player` sets `m_iPersistenceReady = ePersistenceReady::READY_INSECURE` when
// `ns_auth_allow_insecure` is set, with the comment *"actual placeholder
// persistent data is populated in script with InitPersistentData()"*, and
// Northstar.CustomServers calls `InitPersistentData( player )` from
// `CodeCallback_OnClientConnectionCompleted`. The ordering matters: the script
// above runs at connection *Started*, before that initialisation, so the flag
// has to already be set by the engine side.
//
// This port hooks no server authentication at all, so nothing ever sets it and
// the flag stays at NOT_READY. That is what this file supplies, as close to
// PC's insecure path as the port can get.
//
// **The gate.** Both persistence accessors in server.prx consult the same
// engine interface through the same vtable slot before touching a var:
//
//   004810a2  movsx ebx, di                  ; entity index
//   004810a5  mov   rdi, [rip + 0x63c1dc]    ; interface singleton -> 0xabd288
//   004810ac  dec   ebx                      ; entity index - 1 = client #0
//   004810b3  call  qword ptr [rax + 0x5c8]  ; IsPersistentDataAvailable(client)
//   004810b9  test  al, al
//   004810bb  je    0x481174                 ; -> "Persistent data not available"
//
// The second accessor at 0x481319 loads the same singleton (0x63bf68 from
// 0x481320 is also 0xabd288) and calls the same slot, so hooking the slot
// covers both, and any other caller, rather than patching two call sites and
// hoping there is no third.
//
// The vtable is replaced the way the filesystem overlay does it: copy the
// table into module memory, swap the one entry, and repoint the object. No
// page protection is touched, because the object's vtable pointer is ordinary
// writable data.
//
// **What the hook does, and why not simply return true.** The first version
// forced the answer to true. The lobby came up, and then the game crashed
// (access violation at engine+0x2dbd6b) once the lobby shut down. The engine
// function behind the slot, engine.prx 0x2dba90, is:
//
//   if (client >= clientCount)                  return false  ; engine+0x38188c0
//   c = &clients[client]                        ; engine+0x3818680, stride 0x2d738
//   if (c.signonState   (+0x4f0) != 8)          return false  ; not fully connected
//   return c.persistenceReady (+0x6f0) > 2                    ; READY_INSECURE = 3
//
// and the persistence readers repeat that check inline: when it fails they
// take a NULL buffer instead of `client + 0x74a` and read through it. Forcing
// the vtable answer made the callers believe a buffer existed that the readers
// then refused to hand out. That is exactly what happened after the lobby
// shut down and the client slot emptied.
//
// So the hook now does what PC's AuthenticatePlayer does: it sets the client's
// own `persistenceReady` field to READY_INSECURE, and only for a slot that is
// fully connected, then lets the engine's function answer. Every reader
// (through the vtable or inlined) sees one consistent state, a disconnected
// slot still reports unavailable, and a slot the engine has loaded real
// persistence for is left alone.
//
// PC's layout for comparison: stride 0x2d728, m_iPersistenceReady at 0x4a0,
// buffer at 0x4fa. The console build is laid out differently, so these
// offsets come from the function above and are gated on its exact bytes.
constexpr std::uintptr_t kEnginePersistenceAvailableVa = 0x2dba90;
constexpr std::uintptr_t kEngineClientCountVa = 0x38188c0;
constexpr std::uintptr_t kEngineClientArrayVa = 0x3818680;
constexpr std::size_t kEngineClientStride = 0x2d738;
constexpr std::size_t kClientSignonStateOffset = 0x4f0;
constexpr std::size_t kClientPersistenceReadyOffset = 0x6f0;
constexpr std::int32_t kSignonStateFull = 8;
// The CBaseClient inside each client-array element: kickid (engine+0x124380)
// passes element+0x250 to Disconnect, whose signon test at +0x2a0 is this
// file's +0x4f0, and PC's m_iPersistenceReady (0x4a0) is this file's +0x6f0.
constexpr std::size_t kClientObjectOffset = 0x250;
constexpr std::size_t kClientFakePlayerOffset = 0x4d3;  // kickid skips slots where this is set
constexpr std::int32_t kPersistenceReadyInsecure = 3;
constexpr std::uintptr_t kServerPersistenceInterfaceVa = 0xabd288;
constexpr std::uintptr_t kServerPersistenceGateVa = 0x4810a5;
constexpr std::uintptr_t kServerPersistenceGate2Va = 0x481319;
// 0x5c8 / sizeof(void*). The two leading entries are the offset-to-top and
// RTTI pointers that sit in front of every Itanium-ABI vtable and have to be
// carried across with it.
constexpr std::size_t kPersistenceAvailableSlot = 0x5c8 / sizeof(void*);
// GetPlayerUID(client), the slot before it: engine+0x2dba60 returns the
// string at element+0xf750 (PC's CBaseClient::m_UID, 0xf500, from the
// CBaseClient at +0x250) for a fully connected client, and "" otherwise.
constexpr std::size_t kPlayerUidSlot = 0x5c0 / sizeof(void*);
constexpr std::uintptr_t kEngineGetPlayerUidVa = 0x2dba60;
constexpr std::size_t kClientUidStringOffset = 0xf750;
constexpr std::size_t kClientUidStringSize = 32;  // PC: char m_UID[32]
// The account id from the connect packet, as a number: ConnectClient stores it
// (engine+0xe767d, `mov [object+0x2d658], rax`), and the duplicate account
// check compares it (engine+0xe7345, `cmp [element+0x4d3+0x2d3d5], rcx`).
constexpr std::size_t kClientConnectUidOffset = 0x2d8a8;
// GetClientConVarValue(client + 1, name), slot 0x158 (engine+0x2d8d20): the
// client's userinfo KeyValues at element+0x4a8 (PC: CBaseClient::m_ConVars,
// 0x258), read with KeyValues::GetString (engine+0x20a5c0) and FindKey
// (engine+0x20a2f0).
constexpr std::size_t kClientConVarValueSlot = 0x158 / sizeof(void*);
constexpr std::uintptr_t kEngineClientConVarValueVa = 0x2d8d20;
constexpr std::uintptr_t kKeyValuesGetStringVa = 0x20a5c0;
constexpr std::uintptr_t kKeyValuesFindKeyVa = 0x20a2f0;
constexpr std::size_t kClientConVarsOffset = 0x4a8;
// ClientPrintf(entity index, message), slot 0xd8 (engine+0x2d8240): calls
// CBaseClient::ClientPrintf (engine+0xd5b60) with "%s", as PC's
// NSSendClientPrint does, for entity indices 1-32.
constexpr std::size_t kClientPrintSlot = 0xd8 / sizeof(void*);
constexpr std::uintptr_t kEngineClientPrintVa = 0x2d8240;
using EngineClientPrintFn = void (*)(void*, int, const char*);
EngineClientPrintFn g_engineClientPrint = nullptr;
void* g_engineServerObject = nullptr;
constexpr std::size_t kPersistenceVtableSlots = 288;

using PersistenceAvailableFn = bool (*)(void*, int);
PersistenceAvailableFn g_originalPersistenceAvailable = nullptr;
using PlayerUidFn = const char* (*)(void*, int);
PlayerUidFn g_originalPlayerUid = nullptr;
std::uintptr_t g_persistenceVtable[kPersistenceVtableSlots + 2]{};
bool g_persistenceHookInstalled = false;
std::uintptr_t g_engineBaseForPersistence = 0;

// A player's save from Atlas, while it is installed in their slot. PC keeps
// the received pdata in m_PlayerAuthenticationData and the buffer in the
// client; here the buffer is the client's (element+0x74a, PC 0x4fa) and what
// the PS4 layout has no room for - the bytes after 231's structure - is kept
// for the write back (northstar_ps4/pdata_convert.h).
constexpr std::size_t kClientPersistenceBufferOffset = 0x74a;
constexpr std::int32_t kPersistenceReadyRemote = 4;  // PC ePersistenceReady::READY_REMOTE
constexpr int kMaxRemoteSaveSlots = 128;
struct RemoteSave {
    bool installed = false;
    std::uint64_t uid = 0;
    std::string trailing;
    std::string pdata;  // as received, to install again if the engine resets the slot
};
RemoteSave g_remoteSaves[kMaxRemoteSaveSlots];
char* ClientSlot(int client) noexcept;  // below, with the UID code

void InstallRemoteSave(int client, char* slot, std::uint64_t uid, const std::string& pdata) noexcept {
    auto& save = g_remoteSaves[client];
    save.trailing = pdata::InstallFromPc(pdata, reinterpret_cast<std::uint8_t*>(slot + kClientPersistenceBufferOffset));
    save.uid = uid;
    if (&save.pdata != &pdata) save.pdata = pdata;
    save.installed = true;
    *reinterpret_cast<std::int32_t*>(slot + kClientPersistenceReadyOffset) = kPersistenceReadyRemote;
}

// PC installs the pdata in CBaseClient::Connect, before signon, so the client
// receives it with its first persistence data. The engine only sends script
// changes after that, so data written into the buffer later never reaches the
// client (a PC saw its own loadouts as zeros and stopped in sh_loadouts.nut
// IsTitanClassPrime). Called by runtime_atlas_server.inl right after the
// engine has handled the connect request that carried the player's token:
// the slot holding that uid is the one just connected.
bool InstallRemoteSaveAtConnect(std::uint64_t uid, const std::string& pdata) noexcept {
    const auto engine = g_engineBaseForPersistence;
    if (!engine) return false;
    if (!pdata::CanInstall(pdata)) {
        LogFormat("[NorthstarPS4] Atlas pdata for uid %llu not installable (%zu bytes)\n",
            static_cast<unsigned long long>(uid), pdata.size());
        return false;
    }
    const int count = *reinterpret_cast<const std::int32_t*>(engine + kEngineClientCountVa);
    for (int client = 0; client < count && client < kMaxRemoteSaveSlots; ++client) {
        char* slot = reinterpret_cast<char*>(engine + kEngineClientArrayVa) + static_cast<std::size_t>(client) * kEngineClientStride;
        if (*reinterpret_cast<const std::uint64_t*>(slot + kClientConnectUidOffset) != uid ||
            *reinterpret_cast<const std::uint8_t*>(slot + kClientFakePlayerOffset) != 0 ||
            *reinterpret_cast<const std::int32_t*>(slot + kClientSignonStateOffset) < 1)
            continue;
        InstallRemoteSave(client, slot, uid, pdata);
        LogFormat("[NorthstarPS4] client #%d persistence installed from Atlas at connect (%zu bytes, READY_REMOTE, signon %d)\n",
            client, pdata.size(), *reinterpret_cast<const std::int32_t*>(slot + kClientSignonStateOffset));
        return true;
    }
    return false;
}

bool RuntimePersistenceAvailable(void* self, int client) noexcept {
    const auto engine = g_engineBaseForPersistence;
    if (engine && client >= 0 &&
        client < *reinterpret_cast<const std::int32_t*>(engine + kEngineClientCountVa)) {
        auto slot = reinterpret_cast<char*>(engine + kEngineClientArrayVa) +
            static_cast<std::size_t>(client) * kEngineClientStride;
        auto ready = reinterpret_cast<std::int32_t*>(slot + kClientPersistenceReadyOffset);
        const auto signon = *reinterpret_cast<const std::int32_t*>(slot + kClientSignonStateOffset);
        // Only a fully connected client, and only if the engine has not
        // already marked it ready itself.
        if (signon == kSignonStateFull && *ready < kPersistenceReadyInsecure) {
            // PC: AuthenticatePlayer copies the pdata Atlas sent for this
            // connection into the buffer and marks it READY_REMOTE; anyone
            // else is READY_INSECURE (placeholder data from script).
            const auto uid = *reinterpret_cast<const std::uint64_t*>(slot + kClientConnectUidOffset);
            const bool fake = *reinterpret_cast<const std::uint8_t*>(slot + kClientFakePlayerOffset) != 0;
            std::string pdata;
            // Installed at connect, but the engine reset the slot since.
            if (!fake && client < kMaxRemoteSaveSlots && g_remoteSaves[client].installed &&
                g_remoteSaves[client].uid == uid && !g_remoteSaves[client].pdata.empty()) {
                InstallRemoteSave(client, slot, uid, g_remoteSaves[client].pdata);
                LogFormat("[NorthstarPS4] client #%d persistence installed again from Atlas (the engine reset the slot)\n", client);
                return g_originalPersistenceAvailable(self, client);
            }
            if (!fake && client < kMaxRemoteSaveSlots && g_takeRemotePdata && g_takeRemotePdata(uid, pdata)) {
                if (pdata::CanInstall(pdata)) {
                    InstallRemoteSave(client, slot, uid, pdata);
                    LogFormat("[NorthstarPS4] client #%d persistence installed from Atlas late (%zu bytes, READY_REMOTE); "
                        "the client may not receive it\n", client, pdata.size());
                    return g_originalPersistenceAvailable(self, client);
                }
                LogFormat("[NorthstarPS4] client #%d: Atlas pdata not installable (%zu bytes, version %d)\n", client,
                    pdata.size(), pdata.size() >= 4 ? pdata::Version(reinterpret_cast<const std::uint8_t*>(pdata.data())) : 0);
            }
            if (client < kMaxRemoteSaveSlots) g_remoteSaves[client].installed = false;
            *ready = kPersistenceReadyInsecure;
            LogFormat("[NorthstarPS4] client #%d persistence marked READY_INSECURE "
                "(PC AuthenticatePlayer equivalent)\n", client);
        }
    }
    return g_originalPersistenceAvailable(self, client);
}

// PC: ServerAuthenticationManager::WritePersistentData for a READY_REMOTE
// client, on disconnect, early leave and map change. The slot must still hold
// the same player; `keep` leaves the save installed (the player stays).
bool WriteRemoteSave(int client, const char* reason, bool keep) noexcept {
    if (client < 0 || client >= kMaxRemoteSaveSlots || !g_remoteSaves[client].installed || !g_writeRemotePdata)
        return false;
    auto& save = g_remoteSaves[client];
    char* slot = ClientSlot(client);
    const bool samePlayer = slot &&
        *reinterpret_cast<const std::int32_t*>(slot + kClientPersistenceReadyOffset) == kPersistenceReadyRemote &&
        *reinterpret_cast<const std::uint64_t*>(slot + kClientConnectUidOffset) == save.uid;
    if (!samePlayer) {
        save.installed = false;
        return false;
    }
    const std::string out = pdata::ExportToPc(
        reinterpret_cast<const std::uint8_t*>(slot + kClientPersistenceBufferOffset), save.trailing);
    if (out.empty()) {
        LogFormat("[NorthstarPS4] client #%d: not writing pdata (%s): the buffer no longer holds a layout-231 save\n",
            client, reason);
    } else {
        // Only what changed since the save was received or last written: a
        // client that retries its connect request is disconnected and
        // connected again with nothing played, and its unchanged save need not
        // go back (nor overtake a newer write).
        std::size_t changed = 0;
        for (std::size_t i = 0; i < out.size(); ++i)
            if (i >= save.pdata.size() || out[i] != save.pdata[i]) ++changed;
        if (changed == 0 && out.size() == save.pdata.size()) {
            LogFormat("[NorthstarPS4] client #%d: pdata unchanged (%s), nothing to write\n", client, reason);
        } else {
            LogFormat("[NorthstarPS4] client #%d: pdata has %zu changed bytes (%s)\n", client, changed, reason);
            g_writeRemotePdata(save.uid, out, reason);
            save.pdata = out;
        }
    }
    if (!keep) {
        save.installed = false;
        save.pdata.clear();
        save.pdata.shrink_to_fit();
    }
    return !out.empty();
}

// CBaseClient::Disconnect for the client object at element+0x250.
void WriteRemoteSaveOnDisconnect(void* clientObject) noexcept {
    const auto engine = g_engineBaseForPersistence;
    if (!engine || !clientObject) return;
    const auto element = reinterpret_cast<std::uintptr_t>(clientObject) - kClientObjectOffset;
    const auto array = engine + kEngineClientArrayVa;
    if (element < array || (element - array) % kEngineClientStride) return;
    WriteRemoteSave(static_cast<int>((element - array) / kEngineClientStride), "disconnect", false);
}

// Every installed save, at a map change (PC writes when a client reconnects
// for the next map).
void WriteAllRemoteSaves(const char* reason) noexcept {
    for (int client = 0; client < kMaxRemoteSaveSlots; ++client)
        if (g_remoteSaves[client].installed) WriteRemoteSave(client, reason, true);
}

// PC: ServerAuthenticationManager::AuthenticatePlayer copies the connecting
// player's uid into m_UID (`std::to_string(uid)`, 0 for bots), and scripts read
// it back with player.GetUID(). The PS4 engine keeps the uid only as the number
// from the connect packet and leaves the string empty, so GetUID() returned ""
// on a PS4 host. The string is filled from that number the first time it is
// asked for once the client is fully connected; the engine clears it when the
// slot is reused (engine+0xd5709).
char* ClientSlot(int client) noexcept {
    const auto engine = g_engineBaseForPersistence;
    if (!engine || client < 0 || client >= *reinterpret_cast<const std::int32_t*>(engine + kEngineClientCountVa))
        return nullptr;
    return reinterpret_cast<char*>(engine + kEngineClientArrayVa) + static_cast<std::size_t>(client) * kEngineClientStride;
}

const char* ClientUid(int client) noexcept {
    char* slot = ClientSlot(client);
    if (!slot || *reinterpret_cast<const std::int32_t*>(slot + kClientSignonStateOffset) != kSignonStateFull)
        return nullptr;
    char* uid = slot + kClientUidStringOffset;
    if (!uid[0]) {
        const bool fake = *reinterpret_cast<const std::uint8_t*>(slot + kClientFakePlayerOffset) != 0;
        const auto number = fake ? 0 : *reinterpret_cast<const std::uint64_t*>(slot + kClientConnectUidOffset);
        std::snprintf(uid, kClientUidStringSize, "%llu", static_cast<unsigned long long>(number));
    }
    return uid;
}

// PC: GetUserInfoKV*_Internal read `m_ConVars->Get*(key, default)`: the
// default only when the key is missing.
const char* ClientUserInfo(int client, const char* key, const char* fallback, bool* found) noexcept {
    *found = false;
    char* slot = ClientSlot(client);
    void* convars = slot ? *reinterpret_cast<void**>(slot + kClientConVarsOffset) : nullptr;
    if (!convars || !key) return fallback;
    const auto engine = g_engineBaseForPersistence;
    if (!reinterpret_cast<void* (*)(void*, const char*, bool)>(engine + kKeyValuesFindKeyVa)(convars, key, false))
        return fallback;
    *found = true;
    return reinterpret_cast<const char* (*)(void*, const char*, const char*)>(engine + kKeyValuesGetStringVa)(
        convars, key, fallback ? fallback : "");
}

bool ClientPrint(int client, const char* message) noexcept {
    if (!g_engineClientPrint || !message || !ClientSlot(client)) return false;
    g_engineClientPrint(g_engineServerObject, client + 1, message);
    return true;
}

const char* RuntimePlayerUid(void* self, int client) noexcept {
    ClientUid(client);
    return g_originalPlayerUid(self, client);
}

// CBaseClient::Disconnect for a client slot, for NSPS4_DisconnectClient. Like
// kickid, a slot that is not connected (signon < 2) or a bot is left alone.
bool DisconnectClient(int client, const char* reason) noexcept {
    const auto engine = g_engineBaseForPersistence;
    if (!engine || !g_clientDisconnect || client < 0 ||
        client >= *reinterpret_cast<const std::int32_t*>(engine + kEngineClientCountVa)) return false;
    auto slot = reinterpret_cast<char*>(engine + kEngineClientArrayVa) +
        static_cast<std::size_t>(client) * kEngineClientStride;
    if (*reinterpret_cast<const std::int32_t*>(slot + kClientSignonStateOffset) < 2 ||
        *reinterpret_cast<const std::uint8_t*>(slot + kClientFakePlayerOffset) != 0) return false;
    LogFormat("[NorthstarPS4] disconnecting client #%d: %s\n", client, reason);
    g_clientDisconnect(slot + kClientObjectOffset, 1, "%s", reason);
    return true;
}

bool InstallRuntimePersistence() noexcept {
    if (g_persistenceHookInstalled) return true;
    const auto base = g_runtimeServerBase;
    if (base == 0 || g_runtimeServerSpan == 0) return false;

    struct Gate { std::uintptr_t va; const char* bytes; std::size_t size; };
    const Gate gates[] = {
        {kServerPersistenceGateVa,
         "\x48\x8b\x3d\xdc\xc1\x63\x00\xff\xcb\x89\xde\x48\x8b\x07\xff\x90\xc8\x05\x00\x00\x84\xc0\x0f\x84\xb3\x00\x00\x00", 28},
        {kServerPersistenceGate2Va,
         "\x48\x8b\x3d\x68\xbf\x63\x00\x0f\xbf\xdb\xff\xcb\x89\xde\x48\x8b\x07\xff\x90\xc8\x05\x00\x00\x84\xc0\x0f\x84\xe0\x00\x00\x00", 31},
    };
    for (const auto& gate : gates) {
        if (!ValidateEnginePreimage(base, g_runtimeServerSpan, gate.va,
                reinterpret_cast<const std::uint8_t*>(gate.bytes), gate.size)) {
            LogFormat("[NorthstarPS4] persistence gate mismatch va=%lx; lobby persistence unchanged\n", gate.va);
            return false;
        }
    }

    // The singleton is constructed while the server starts, so this can be
    // reached before it exists; the caller retries on the next VM creation.
    void* object = *reinterpret_cast<void**>(base + kServerPersistenceInterfaceVa);
    if (!object) {
        LogFormat("[NorthstarPS4] persistence interface not constructed yet\n");
        return false;
    }
    auto vtable = *reinterpret_cast<void***>(object);
    if (!vtable || !AuthAddressReadable(vtable) ||
        !AuthAddressReadable(vtable + kPersistenceVtableSlots - 1)) {
        LogFormat("[NorthstarPS4] persistence vtable unreadable object=%p vtable=%p\n", object, vtable);
        return false;
    }
    auto original = reinterpret_cast<PersistenceAvailableFn>(vtable[kPersistenceAvailableSlot]);
    if (!original) {
        LogFormat("[NorthstarPS4] persistence vtable slot %zu is null\n", kPersistenceAvailableSlot);
        return false;
    }
    // The implementation lives in engine.prx. Its base is recovered from the
    // function's own address and then proven by matching every byte of it:
    // the client offsets this hook writes through are only valid for exactly
    // this function, so a mismatch leaves persistence untouched.
    constexpr std::uint8_t implementation[] = {
        0x39,0x35,0x2a,0xce,0x53,0x03,0x7e,0x27,0x48,0x63,0xc6,0x48,0x8d,0x0d,
        0xde,0xcb,0x53,0x03,0x48,0x69,0xc0,0x38,0xd7,0x02,0x00,0x83,0xbc,0x08,
        0xf0,0x04,0x00,0x00,0x08,0x75,0x0f,0x83,0xbc,0x08,0xf0,0x06,0x00,0x00,
        0x02,0x0f,0x9f,0xc0,0xc3,0x31,0xc0,0xc3,0x31,0xc0,0xc3,
    };
    constexpr std::size_t kEngineTextSpan = 0x398000;
    const auto engine = reinterpret_cast<std::uintptr_t>(original) - kEnginePersistenceAvailableVa;
    if (!ValidateEnginePreimage(engine, kEngineTextSpan, kEnginePersistenceAvailableVa,
            implementation, sizeof(implementation)) ||
        !AuthAddressReadable(reinterpret_cast<const void*>(engine + kEngineClientCountVa))) {
        LogFormat("[NorthstarPS4] persistence implementation mismatch at %p; lobby persistence unchanged\n",
            reinterpret_cast<void*>(original));
        return false;
    }
    g_engineBaseForPersistence = engine;
    g_disconnectClient = DisconnectClient;

    // GetPlayerUID, gated on its bytes and on the connect uid's offset in the
    // duplicate account check (the check itself may already be patched by the
    // host options, so only the loop in front of it is compared).
    constexpr std::uint8_t uidGetter[] = {0x48, 0x63, 0xc6, 0x48, 0x8d, 0x0d, 0x16, 0xcc, 0x53, 0x03, 0x48, 0x69, 0xc0,
        0x38, 0xd7, 0x02, 0x00, 0x83, 0xbc, 0x08, 0xf0, 0x04, 0x00, 0x00, 0x08, 0x75, 0x09, 0x48, 0x8d, 0x84, 0x08, 0x50,
        0xf7, 0x00, 0x00, 0xc3, 0x48, 0x8d, 0x05, 0x30, 0x17, 0x07, 0x00, 0xc3};
    constexpr std::uint8_t uidLoop[] = {0x48, 0x8d, 0x35, 0x1b, 0x18, 0x73, 0x03, 0x31, 0xff, 0x83, 0x7e, 0x1d, 0x02,
        0x7c, 0x12, 0x80, 0x3e, 0x00, 0x75, 0x0d};
    const bool uidMatches =
        reinterpret_cast<std::uintptr_t>(vtable[kPlayerUidSlot]) == engine + kEngineGetPlayerUidVa &&
        ValidateEnginePreimage(engine, kEngineTextSpan, kEngineGetPlayerUidVa, uidGetter, sizeof(uidGetter)) &&
        ValidateEnginePreimage(engine, kEngineTextSpan, 0xe7331, uidLoop, sizeof(uidLoop));

    for (std::size_t i = 0; i < kPersistenceVtableSlots + 2; ++i)
        g_persistenceVtable[i] = reinterpret_cast<std::uintptr_t>(vtable[i - 2]);
    g_originalPersistenceAvailable = original;
    g_persistenceVtable[kPersistenceAvailableSlot + 2] =
        reinterpret_cast<std::uintptr_t>(&RuntimePersistenceAvailable);
    constexpr std::uint8_t userInfoBytes[] = {0xff, 0xce, 0x4c, 0x8d, 0x25, 0x2c, 0xf9, 0x53, 0x03, 0x48, 0x63,
        0xc6, 0x4c, 0x69, 0xf8, 0x38, 0xd7, 0x02, 0x00, 0x4b, 0x8b, 0xbc, 0x27, 0xa8, 0x04, 0x00, 0x00, 0x48, 0x85, 0xff,
        0x74, 0x32, 0x41, 0x80, 0x3e, 0x00, 0x74, 0x2c, 0x48, 0x8d, 0x15, 0x43, 0x44, 0x07, 0x00, 0x4c, 0x89, 0xf6, 0xe8,
        0x40, 0x18, 0xf3, 0xff, 0x48, 0x89, 0xc3, 0x80, 0x3b, 0x00, 0x75, 0x15, 0x4b, 0x8d, 0x84, 0x27, 0xa8, 0x04, 0x00,
        0x00, 0x31, 0xd2, 0x4c, 0x89, 0xf6, 0x48, 0x8b, 0x38, 0xe8, 0x53, 0x15, 0xf3, 0xff};
    if (reinterpret_cast<std::uintptr_t>(vtable[kClientConVarValueSlot]) == engine + kEngineClientConVarValueVa &&
        ValidateEnginePreimage(engine, kEngineTextSpan, kEngineClientConVarValueVa + 0x2b, userInfoBytes,
            sizeof(userInfoBytes)))
        g_clientUserInfo = ClientUserInfo;
    else
        LogFormat("[NorthstarPS4] userinfo natives refused: engine profile mismatch\n");
    constexpr std::uint8_t clientPrintBytes[] = {0x8d, 0x46, 0xff, 0x0f, 0xb7, 0xc0, 0x83, 0xf8, 0x1f, 0x76, 0x01,
        0xc3, 0x48, 0x0f, 0xbf, 0xc6, 0x48, 0x8d, 0x0d, 0x29, 0x04, 0x54, 0x03, 0x48, 0x8d, 0x35, 0xa1, 0x3d, 0x07, 0x00,
        0x48, 0x69, 0xc0, 0x38, 0xd7, 0x02, 0x00, 0x48, 0x8d, 0xbc, 0x08};
    if (reinterpret_cast<std::uintptr_t>(vtable[kClientPrintSlot]) == engine + kEngineClientPrintVa &&
        ValidateEnginePreimage(engine, kEngineTextSpan, kEngineClientPrintVa, clientPrintBytes, sizeof(clientPrintBytes))) {
        g_engineClientPrint = reinterpret_cast<EngineClientPrintFn>(vtable[kClientPrintSlot]);
        g_engineServerObject = object;
        g_clientPrint = ClientPrint;
    } else {
        LogFormat("[NorthstarPS4] NSSendClientPrint refused: engine profile mismatch\n");
    }
    if (uidMatches) {
        g_originalPlayerUid = reinterpret_cast<PlayerUidFn>(vtable[kPlayerUidSlot]);
        g_persistenceVtable[kPlayerUidSlot + 2] = reinterpret_cast<std::uintptr_t>(&RuntimePlayerUid);
        g_clientUid = ClientUid;
    } else {
        LogFormat("[NorthstarPS4] player UID hook refused: engine profile mismatch\n");
    }
    *reinterpret_cast<void**>(object) = g_persistenceVtable + 2;

    g_persistenceHookInstalled = true;
    LogFormat("[NorthstarPS4] persistence availability hook installed object=%p original=%p slot=%zu\n",
        object, reinterpret_cast<void*>(original), kPersistenceAvailableSlot);
    return true;
}
