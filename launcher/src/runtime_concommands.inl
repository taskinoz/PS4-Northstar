// Native console commands that NorthstarLauncher adds to the engine.
//
// Northstar's scripts rely on commands the stock game does not have, and on PS4
// they silently did nothing:
//
//   - `setplaylist` / `playlist` (shared/playlist.cpp). "Launch Northstar" runs
//     `setplaylist tdm` before `map mp_lobby`. Without it the playlist stays at
//     whatever was last set - "private_match" after a private match - so quitting
//     to the main menu and entering multiplayer again rebuilt the private lobby
//     (_lobby.gnut keys the lobby type off GetCurrentPlaylistName()).
//   - `ns_start_reauth_and_leave_to_lobby` (shared/misccommands.cpp). A server
//     ends every "leave match" with ClientCommand( player, this ). Without it the
//     player was left in the match with the leave already acknowledged.
//
// **Engine profile.** `ConCommand::ConCommand(this, name, callback, help, flags,
// completion)` is engine 0x204e60: the engine's own static constructors call it
// with exactly that argument order (e.g. `disconnect` at 0x122bd1), and it lays
// the object out as PC's ConCommand does - name +0x18, help +0x20, flags +0x28,
// callback +0x40, completion +0x48 - 0x58 bytes apart. When the cvar system is
// already up (always, by the time this runs) it registers the command itself.
// The callback receives a CCommand laid out as on PC: argc +0, argv0 length +8,
// ArgS buffer +0x10, argv pointers +0x410 (connect's callback at 0x62bd0,
// Tokenize at 0x204b70). `SetCurrentPlaylist(const char*)` is engine 0x14a3a0:
// server.prx's SetCurrentPlaylist native (0x6ce0a0) calls slot 81 of the engine
// server interface, whose vtable (0x3acfd8, identified by slot 185 = 0x2dba90,
// the persistence hook's target) holds a thunk at 0x2d9250 that jumps there.
// Flag values match PC's (`connect` 0x20000 = DONTRECORD; a stock command with
// 0x50020000 = SERVER_CAN_EXECUTE | CLIENTCMD_CAN_EXECUTE | DONTRECORD).
constexpr std::uintptr_t kConCommandConstructorVa = 0x204e60;
constexpr std::uintptr_t kSetCurrentPlaylistVa = 0x14a3a0;
constexpr int kFcvarNone = 0;
constexpr int kFcvarServerCanExecute = 1 << 28;

struct CCommandView {
    std::int64_t argc;
    std::int64_t argv0Size;
    char argS[512];
    char argvBuffer[512];
    const char* argv[64];
};
static_assert(offsetof(CCommandView, argv) == 0x410, "CCommand layout");

using ConCommandCallbackFn = void (*)(const CCommandView*);
using ConCommandConstructorFn = void (*)(void*, const char*, ConCommandCallbackFn, const char*, int, void*);
using SetCurrentPlaylistFn = bool (*)(const char*);

SetCurrentPlaylistFn g_setCurrentPlaylist = nullptr;

bool SetPlaylist(const char* name) noexcept {
    if (!g_setCurrentPlaylist || !name) return false;
    const bool ok = g_setCurrentPlaylist(name);
    // PC logs every change from its SetCurrentPlaylist hook.
    LogFormat("[NorthstarPS4] %s playlist %s\n", ok ? "set" : "could not set", name);
    return ok;
}

// PC: ConCommand_playlist. Registered as both `playlist` and `setplaylist`.
void ConCommandSetPlaylist(const CCommandView* args) {
    if (args->argc < 2) return;
    SetPlaylist(args->argv[1]);
}

// PC's pair is ns_start_reauth_and_leave_to_lobby, which authenticates with the
// player's own local server, and ns_end_reauth_and_leave_to_lobby, which runs
// after that succeeds: sets serverfilter, SetCurrentPlaylist("tdm") and starts
// a new game on mp_lobby - but only while a CLIENT VM exists, "mainly just to
// allow players to cancel this". Local auth on PS4 needs only an imported
// identity (see TryLocalAuth), so both halves run here. The new game goes
// through the UI VM's ClientCommand, the same path "Launch Northstar" uses.
void ConCommandLeaveToLobby(const CCommandView*) {
    if (!AtlasIdentityReady()) {
        LogFormat("[NorthstarPS4] leave to lobby refused: %s\n", AtlasIdentityMessage());
        return;
    }
    if (!g_runtimeClientLifecycle.owner) {
        LogFormat("[NorthstarPS4] leave to lobby skipped: no CLIENT VM\n");
        return;
    }
    void* uiOwner = g_runtimeUiLifecycle.owner;
    void* uiVm = uiOwner ? *reinterpret_cast<void**>(static_cast<char*>(uiOwner) + 8) : nullptr;
    if (!uiVm) {
        LogFormat("[NorthstarPS4] leave to lobby failed: no UI VM\n");
        return;
    }
    SetPlaylist("tdm");
    const bool queued = uiapi::CallScriptWithString(uiVm, "NSPS4_ClientCommand", "map mp_lobby");
    LogFormat("[NorthstarPS4] leave to lobby: %s\n", queued ? "map mp_lobby queued" :
        "NSPS4_ClientCommand missing (Northstar.PS4 not loaded?)");
}

// `map` with a map the game does not have (e.g. `mp_box`, which neither the PS4
// nor the PC game ships) left the game with no level and no menu. While
// connected, the engine's callback (0x1224a0) tears the current game down
// (0x120d80) before it checks the map, and a failed check just returns. PC
// reports "map load failed" and stays where it is.
//
// The guard decides whether the map exists before the original runs, without
// calling the engine's map check: a first attempt that called
// VEngineServer::IsMapValid (0x2d7470) from here stopped the main thread. A map
// is present when any of these holds:
//   - its retail archive exists: /app0/vpk_ps4/englishclient_<map>.bsp.pak000_dir.vpk
//     (per-map archives are mounted only once the load starts, so the
//     filesystem cannot see their contents yet);
//   - an enabled mod ships an archive for it (stem client_<map>.bsp);
//   - maps/<map>.bsp opens through the game filesystem (maps inside common
//     archives, such as mp_lobby in mp_common, and loose mod maps).
// Names outside [A-Za-z0-9_] are passed through untouched.
constexpr std::uintptr_t kMapCommandObjectVa = 0x3ef2ce8;
constexpr std::uintptr_t kMapCommandNameVa = 0x33e7f0;
constexpr std::uintptr_t kMapCommandCallbackVa = 0x1224a0;

void* g_fsInstance = nullptr;  // VFileSystem017, set by the filesystem probe
ConCommandCallbackFn g_originalMapCommand = nullptr;

bool PlainMapName(const char* name) noexcept {
    if (!name || !*name) return false;
    for (const char* c = name; *c; ++c)
        if (!((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9') || *c == '_'))
            return false;
    return std::strlen(name) < 64;
}

bool MapPresent(const char* name) noexcept {
    char path[160];
    std::snprintf(path, sizeof(path), "/app0/vpk_ps4/englishclient_%s.bsp.pak000_dir.vpk", name);
    const int fd = open(path, 0);
    if (fd >= 0) { close(fd); return true; }
    const std::string stem = std::string("client_") + name + ".bsp";
    for (const auto& vpk : g_modVpks)
        if (vpk.stem == stem) return true;
    if (g_fsInstance && g_originalFsOpenEx && g_originalFsClose) {
        std::snprintf(path, sizeof(path), "maps/%s.bsp", name);
        void* handle = g_originalFsOpenEx(g_fsInstance, path, "rb", 0, "GAME", nullptr);
        if (handle) {
            g_originalFsClose(static_cast<char*>(g_fsInstance) + 8, handle);
            return true;
        }
    }
    return false;
}

void GuardedMapCommand(const CCommandView* args) {
    if (args->argc >= 2 && PlainMapName(args->argv[1]) && !MapPresent(args->argv[1])) {
        LogFormat("[NorthstarPS4] map load failed: %s not found\n", args->argv[1]);
        return;
    }
    g_originalMapCommand(args);
}

void InstallMapCommandGuard(std::uintptr_t engineBase, std::size_t engineSize) noexcept {
    constexpr std::uint8_t callbackBytes[] = {0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56, 0x53, 0x50};
    auto object = reinterpret_cast<std::uintptr_t*>(engineBase + kMapCommandObjectVa);
    if (!ValidateEnginePreimage(engineBase, engineSize, kMapCommandCallbackVa, callbackBytes, sizeof(callbackBytes)) ||
        object[0x18 / 8] != engineBase + kMapCommandNameVa || object[0x40 / 8] != engineBase + kMapCommandCallbackVa) {
        LogFormat("[NorthstarPS4] map command guard refused: engine profile mismatch\n");
        return;
    }
    g_originalMapCommand = reinterpret_cast<ConCommandCallbackFn>(object[0x40 / 8]);
    object[0x40 / 8] = reinterpret_cast<std::uintptr_t>(&GuardedMapCommand);
    LogFormat("[NorthstarPS4] map command guard installed\n");
}

bool g_nativeConCommandsRegistered = false;

void RegisterNativeConCommands(std::uintptr_t engineBase, std::size_t engineSize) noexcept {
    if (g_nativeConCommandsRegistered) return;
    if (!engineBase) {
        LogFormat("[NorthstarPS4] native concommands skipped: engine base unknown\n");
        return;
    }
    // Both gates avoid rip-relative operands, so they hold for this build only.
    constexpr std::uint8_t constructorBytes[] = {
        0x48, 0xc7, 0x40, 0x08, 0x00, 0x00, 0x00, 0x00, 0x4c, 0x89, 0x18, 0x48, 0x89, 0x50, 0x40,
        0x41, 0x0f, 0x95, 0xc2, 0x49, 0x0f, 0x45, 0xf9, 0x8a, 0x50, 0x50, 0x48, 0x89, 0x78, 0x48,
    };
    constexpr std::uint8_t playlistBytes[] = {
        0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x53, 0x50,
        0x49, 0x89, 0xfe, 0x41, 0x80, 0x3e, 0x00, 0x0f, 0x84, 0xf1, 0x00, 0x00, 0x00,
    };
    if (!ValidateEnginePreimage(engineBase, engineSize, kConCommandConstructorVa + 0x14,
            constructorBytes, sizeof(constructorBytes)) ||
        !ValidateEnginePreimage(engineBase, engineSize, kSetCurrentPlaylistVa,
            playlistBytes, sizeof(playlistBytes))) {
        LogFormat("[NorthstarPS4] native concommands refused: engine profile mismatch\n");
        return;
    }
    g_setCurrentPlaylist = reinterpret_cast<SetCurrentPlaylistFn>(engineBase + kSetCurrentPlaylistVa);
    auto construct = reinterpret_cast<ConCommandConstructorFn>(engineBase + kConCommandConstructorVa);

    struct Definition { const char* name; ConCommandCallbackFn callback; const char* help; int flags; };
    static const Definition definitions[] = {
        {"playlist", ConCommandSetPlaylist, "Sets the current playlist", kFcvarNone},
        {"setplaylist", ConCommandSetPlaylist, "Sets the current playlist", kFcvarNone},
        {"ns_start_reauth_and_leave_to_lobby", ConCommandLeaveToLobby,
            "called by the server, used to reauth and return the player to lobby when leaving a game",
            kFcvarServerCanExecute},
    };
    // The engine keeps pointers into these for the life of the process. Zeroed
    // static storage is fine: the constructor writes every field it uses.
    alignas(16) static std::uint8_t objects[sizeof(definitions) / sizeof(definitions[0])][0x60]{};
    for (std::size_t i = 0; i < sizeof(definitions) / sizeof(definitions[0]); ++i) {
        construct(objects[i], definitions[i].name, definitions[i].callback, definitions[i].help,
            definitions[i].flags, nullptr);
        LogFormat("[NorthstarPS4] native concommand registered: %s\n", definitions[i].name);
    }
    g_nativeConCommandsRegistered = true;
    InstallMapCommandGuard(engineBase, engineSize);
}
