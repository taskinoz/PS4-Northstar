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

// `tdm` is what both roads to the local lobby set before `map mp_lobby`: the
// leave-to-lobby command and "Launch Northstar". The local server takes its
// game mode from `mp_gamemode`, and that still holds the last remote server's
// replicated value: after a Parkour server the lobby started as `pk` and ran
// the gamemode's scripts in the lobby; after an attrition server, as `aitdm`.
// PC's engine reverts replicated convars on disconnect, so its lobby is `tdm`;
// the PS4 build keeps them. So the lobby's mode is set with its playlist.
void ResetLobbyGameMode() noexcept {
    if (!g_modConVarCvar || !g_modConVarFindVar) return;
    void* gamemode = g_modConVarFindVar(g_modConVarCvar, "mp_gamemode");
    const char* current = gamemode ? ReadConVarValue(gamemode) : nullptr;
    if (!current || !std::strcmp(current, "tdm")) return;
    char previous[64];
    std::snprintf(previous, sizeof(previous), "%s", current);
    if (SetConVarString(gamemode, "tdm"))
        LogFormat("[NorthstarPS4] mp_gamemode %s -> tdm for the tdm playlist\n", previous);
}

// PC: chatcommand.cpp registers `say` and `say_team`, which pass the command's
// argument text to ClientSayText as in-game chat.
constexpr std::uintptr_t kClientSayTextVa = 0x473e0;

const char* CommandArgText(const CCommandView* args) noexcept {
    const char* text = args->argS + args->argv0Size;
    while (*text == ' ' || *text == '\t') ++text;
    return text;
}

void ConCommandSay(const CCommandView* args) {
    if (args->argc >= 2 && !SendChat(CommandArgText(args), false))
        LogFormat("[NorthstarPS4] say: not in a match\n");
}

void ConCommandSayTeam(const CCommandView* args) {
    if (args->argc >= 2 && !SendChat(CommandArgText(args), true))
        LogFormat("[NorthstarPS4] say_team: not in a match\n");
}

bool SetPlaylist(const char* name) noexcept {
    if (!g_setCurrentPlaylist || !name) return false;
    const bool ok = g_setCurrentPlaylist(name);
    // PC logs every change from its SetCurrentPlaylist hook.
    LogFormat("[NorthstarPS4] %s playlist %s\n", ok ? "set" : "could not set", name);
    if (ok && !std::strcmp(name, "tdm")) ResetLobbyGameMode();
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

// `reload_localization` (engine+0x812e0, a jump into the localize singleton's
// reload) rebuilds the token table while other threads read it; on PS4 the
// first call faulted at localize+0x7aed, a hash-chain walk on another thread.
// Northstar's ReloadMods() calls it after every mod change, so it is replaced
// with the part Northstar needs: adding the newly enabled mods' files, which
// only inserts and never clears the table. Tokens of mods disabled by the
// reload stay loaded until the next boot.
constexpr std::uintptr_t kReloadLocalizationObjectVa = 0x1a1d2c0;
constexpr std::uintptr_t kReloadLocalizationNameVa = 0x33357d;
constexpr std::uintptr_t kReloadLocalizationCallbackVa = 0x812e0;

void ModReloadLocalization(const CCommandView*) {
    std::int32_t total = 0, loaded = 0;
    AddModLocalisationFiles(total, loaded);
    LogFormat("[NorthstarPS4] reload_localization: %d new mod file(s), %d loaded; full engine reload skipped\n",
        total, loaded);
}

void InstallReloadLocalizationGuard(std::uintptr_t engineBase, std::size_t engineSize) noexcept {
    // mov rax,[rdi]; jmp [rax+0xd8] - after the rip-relative load of the singleton.
    constexpr std::uint8_t callbackBytes[] = {0x48, 0x8b, 0x07, 0xff, 0xa0, 0xd8, 0x00, 0x00, 0x00};
    auto object = reinterpret_cast<std::uintptr_t*>(engineBase + kReloadLocalizationObjectVa);
    if (!ValidateEnginePreimage(engineBase, engineSize, kReloadLocalizationCallbackVa + 7, callbackBytes, sizeof(callbackBytes)) ||
        object[0x18 / 8] != engineBase + kReloadLocalizationNameVa ||
        object[0x40 / 8] != engineBase + kReloadLocalizationCallbackVa) {
        LogFormat("[NorthstarPS4] reload_localization guard refused: engine profile mismatch\n");
        return;
    }
    object[0x40 / 8] = reinterpret_cast<std::uintptr_t>(&ModReloadLocalization);
    LogFormat("[NorthstarPS4] reload_localization guard installed\n");
}

// PC Northstar reads extra launch arguments from ns_startup_args.txt. The PS4
// has no command line to add to, so the same file lives in the app's storage.
constexpr const char* kStartupArgsFile = "/data/northstar_ps4/ns_startup_args.txt";

// PC's own location, the game folder, is read too: it is /app0 here, which a
// user can write to on the emulator but not on hardware.
constexpr const char* kGameStartupArgsFile = "/app0/ns_startup_args.txt";

bool StartupArgPresent(const char* arg) noexcept {
    for (const char* file : {kGameStartupArgsFile, kStartupArgsFile}) {
        char text[2048];
        std::size_t size = 0;
        if (!ReadFileIntoBuffer(file, text, sizeof(text), size)) continue;
        const std::size_t length = std::strlen(arg);
        for (const char* at = std::strstr(text, arg); at; at = std::strstr(at + 1, arg)) {
            const bool startOk = at == text || std::isspace(static_cast<unsigned char>(at[-1]));
            const bool endOk = at[length] == '\0' || std::isspace(static_cast<unsigned char>(at[length]));
            if (startOk && endOk) {
                LogFormat("[NorthstarPS4] startup argument %s from %s\n", arg, file);
                return true;
            }
        }
    }
    return false;
}

// Options for a PS4-hosted match, which PC sets on its server.
//
// **Client DLL CRC.** A listen server sends the CRC of its own client module
// in the server info, and a PC client compares it with its client.dll: "Your
// .dll [..\bin\x64_retail\client.dll] differs from the server's." (PC engine
// 0x728c0). A PS4 host sends the CRC of bin/ps4_retail/client.prx, which no PC
// can match. Clients skip the check when the value is -1, which is what a
// dedicated server sends: the engine's GetServerClientCRC (engine+0x10d180)
// returns -1 for one, and otherwise returns a cached value at engine+0x3ef01a4
// before computing the file's CRC. Its only other caller is the crash-report
// upload. Seeding the cache with -1 makes a PS4 host send what a dedicated
// server sends, for PC and PS4 clients alike.
//
// **Duplicate accounts.** PC's -allowdupeaccounts (serverauthentication.cpp
// patches engine 0x114510) lets clients with the same account join one server,
// which is how one player tests with two machines; the engine otherwise
// rejects the second with "Player's account is already on the server". On PS4
// the check is `cmp [rsi+0x2d3d5], rcx; je reject` at engine+0xe7345, inside
// the loop over connected clients. It becomes a jump to DuplicateAccountCheck,
// which does the comparison and consults `ns_allow_duplicate_accounts` on every
// connect, so the private lobby can switch it while hosting. -allowdupeaccounts
// in ns_startup_args.txt starts it at 1.
//
// **Insecure.** PC servers with ns_auth_allow_insecure 0 accept only players
// Atlas has authenticated for them. A PS4 host is not registered with Atlas, so
// it cannot check that; at 0 it accepts only its own player, and at 1 anyone
// (see RuntimePersistenceAvailable). PC's autoexec_ns_server.cfg sets 0.
constexpr std::uintptr_t kServerClientCrcCacheVa = 0x3ef01a4;
constexpr std::uintptr_t kServerClientCrcFunctionVa = 0x10d1aa;
constexpr std::uintptr_t kDuplicateCheckVa = 0xe7345;
constexpr std::uintptr_t kDuplicateNextVa = 0xe7352;
constexpr std::uintptr_t kDuplicateRejectVa = 0xe73ec;
constexpr std::uintptr_t kClientDisconnectVa = 0xd75f0;
constexpr std::int32_t kConVarIntValueOffset = 0x5c;

extern "C" {
std::uintptr_t g_duplicateNext = 0;
std::uintptr_t g_duplicateReject = 0;
void* g_duplicateAccountsConVar = nullptr;
}
void* g_allowInsecureConVar = nullptr;

bool ConVarIsSet(void* convar) noexcept {
    return convar && *reinterpret_cast<const std::int32_t*>(static_cast<char*>(convar) + kConVarIntValueOffset) != 0;
}

// Replaces the 13 bytes at engine+0xe7345. rax holds the loop bound there, so
// it is saved around the convar read.
__attribute__((naked)) void DuplicateAccountCheck() {
    asm volatile(
        "cmpq %rcx, 0x2d3d5(%rsi)\n\t"
        "jne 1f\n\t"
        "pushq %rax\n\t"
        "movq g_duplicateAccountsConVar(%rip), %rax\n\t"
        "testq %rax, %rax\n\t"
        "jz 2f\n\t"
        "cmpl $0, 0x5c(%rax)\n\t"
        "je 2f\n\t"
        "popq %rax\n\t"
        "1:\n\t"
        "jmpq *g_duplicateNext(%rip)\n\t"
        "2:\n\t"
        "popq %rax\n\t"
        "jmpq *g_duplicateReject(%rip)\n\t");
}

bool WriteEngineCode(std::uintptr_t address, const void* bytes, std::size_t size) noexcept {
    const auto first = address & ~std::uintptr_t(0x3fff);
    const auto last = (address + size - 1) & ~std::uintptr_t(0x3fff);
    const std::size_t span = last - first + 0x4000;
    if (sceKernelMprotect(reinterpret_cast<void*>(first), span, 7) != 0) return false;
    std::memcpy(reinterpret_cast<void*>(address), bytes, size);
    return sceKernelMprotect(reinterpret_cast<void*>(first), span, 5) == 0;
}

void InstallHostOptions(std::uintptr_t engineBase, std::size_t engineSize) noexcept {
    // Client CRC: gated on the function's dedicated/cache branches, whose
    // rip-relative operands pin the cache address.
    constexpr std::uint8_t crcBytes[] = {
        0x83, 0x78, 0x5c, 0x00, 0x74, 0x14, 0xc7, 0x05, 0xea, 0x2f, 0xde, 0x03, 0xff, 0xff, 0xff, 0xff,
        0xb8, 0xff, 0xff, 0xff, 0xff, 0xe9, 0xf9, 0x01, 0x00, 0x00, 0x8b, 0x05, 0xda, 0x2f, 0xde, 0x03};
    if (ValidateEnginePreimage(engineBase, engineSize, kServerClientCrcFunctionVa, crcBytes, sizeof(crcBytes))) {
        *reinterpret_cast<std::int32_t*>(engineBase + kServerClientCrcCacheVa) = -1;
        LogFormat("[NorthstarPS4] hosted servers send client CRC -1 (as a dedicated server does)\n");
    } else {
        LogFormat("[NorthstarPS4] client CRC override refused: engine profile mismatch\n");
    }

    constexpr std::uint8_t disconnectBytes[] = {0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54,
        0x53, 0x48, 0x81, 0xec, 0xe8, 0x04, 0x00, 0x00, 0x48, 0x89, 0xfb, 0x89};
    if (ValidateEnginePreimage(engineBase, engineSize, kClientDisconnectVa, disconnectBytes, sizeof(disconnectBytes)))
        g_clientDisconnect = reinterpret_cast<ClientDisconnectFn>(engineBase + kClientDisconnectVa);

    if (g_modConVarCvar && g_modConVarFindVar) {
        g_allowInsecureConVar = g_modConVarFindVar(g_modConVarCvar, "ns_auth_allow_insecure");
        const bool startAllowed = StartupArgPresent("-allowdupeaccounts");
        alignas(16) static std::uint8_t duplicateConVar[0x90]{};
        g_duplicateAccountsConVar = g_modConVarFindVar(g_modConVarCvar, "ns_allow_duplicate_accounts");
        if (!g_duplicateAccountsConVar && g_modConVarConstructor) {
            g_modConVarConstructor(duplicateConVar, "ns_allow_duplicate_accounts", startAllowed ? "1" : "0", 0,
                "Let clients with the same account join this server (PC: -allowdupeaccounts)", nullptr);
            g_duplicateAccountsConVar = g_modConVarFindVar(g_modConVarCvar, "ns_allow_duplicate_accounts");
        }
    }

    constexpr std::uint8_t checkBytes[] = {0x48, 0x39, 0x8e, 0xd5, 0xd3, 0x02, 0x00, 0x0f, 0x84, 0x9a, 0x00, 0x00, 0x00};
    const auto check = engineBase + kDuplicateCheckVa;
    const auto distance = static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(&DuplicateAccountCheck)) -
        static_cast<std::int64_t>(check + 5);
    if (!g_duplicateAccountsConVar ||
        !ValidateEnginePreimage(engineBase, engineSize, kDuplicateCheckVa, checkBytes, sizeof(checkBytes)) ||
        distance < -2147483648LL || distance > 2147483647LL) {
        LogFormat("[NorthstarPS4] duplicate account option refused: engine profile mismatch\n");
        return;
    }
    g_duplicateNext = engineBase + kDuplicateNextVa;
    g_duplicateReject = engineBase + kDuplicateRejectVa;
    std::uint8_t jump[sizeof(checkBytes)];
    std::memset(jump, 0x90, sizeof(jump));
    jump[0] = 0xe9;
    const auto rel = static_cast<std::int32_t>(distance);
    std::memcpy(jump + 1, &rel, sizeof(rel));
    if (!WriteEngineCode(check, jump, sizeof(jump))) {
        LogFormat("[NorthstarPS4] duplicate account option failed: mprotect\n");
        return;
    }
    LogFormat("[NorthstarPS4] duplicate account check hooked; ns_allow_duplicate_accounts=%d\n",
        ConVarIsSet(g_duplicateAccountsConVar) ? 1 : 0);
}

// The client's SayText handler (client+0x1db690) drops every message while
// the engine's text-restriction check (engine+0x2bf450) answers true. That
// check returns `debug_force_textRestriction != 0` when the convar is 0 or
// more, and the console's restriction flag (engine+0x3e79e5, 1 here) when it
// is negative, its default of -1. PC Northstar has no such gate - players mute
// each other instead - so the override is set to 0.
void AllowTextChat() noexcept {
    if (!g_modConVarCvar || !g_modConVarFindVar) return;
    void* restriction = g_modConVarFindVar(g_modConVarCvar, "debug_force_textRestriction");
    const bool ok = restriction && SetConVarString(restriction, "0");
    LogFormat("[NorthstarPS4] text chat %s (debug_force_textRestriction 0)\n", ok ? "allowed" : "could not be allowed");
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
        {"say", ConCommandSay, "Enters a message in public chat", kFcvarNone},
        {"say_team", ConCommandSayTeam, "Enters a message in team chat", kFcvarNone},
    };
    // ClientSayText: gated on its prologue and the start of the message setup.
    constexpr std::uint8_t sayTextBytes[] = {0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56, 0x41, 0x54, 0x53,
        0x48, 0x81, 0xec, 0x30, 0x01, 0x00, 0x00};
    if (ValidateEnginePreimage(engineBase, engineSize, kClientSayTextVa, sayTextBytes, sizeof(sayTextBytes))) {
        g_clientSayText = reinterpret_cast<ClientSayTextFn>(engineBase + kClientSayTextVa);
        g_clientStateSlot = engineBase + 0x9f9618;
    }
    else
        LogFormat("[NorthstarPS4] ClientSayText refused: engine profile mismatch; chat cannot be sent\n");
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
    InstallReloadLocalizationGuard(engineBase, engineSize);
    InstallHostOptions(engineBase, engineSize);
    AllowTextChat();
}
