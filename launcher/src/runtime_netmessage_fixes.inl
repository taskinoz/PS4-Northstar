// Netmessage exploit fixes (PC shared/exploit_fixes/exploitfixes.cpp).
//
// PC hooks these functions directly. Here they are reached through the
// messages' vtables: each netmessage's GetName (`lea rax, [rip+name]; ret`)
// leads to its vtable in the engine's read-only data, laid out as in Source:
// slot 4 Process, 5 ReadFromBuffer, 6 WriteToBuffer, 11 GetName. Each slot is
// checked against the expected engine function before it is replaced, so a
// different engine build is refused.
//
//   clc_Screenshot     vtable 0x3b0938   read 0x1b86b0, write 0x1b8620
//   clc_CmdKeyValues   vtable 0x3af508   read 0x1ae8d0 (-> Base read 0x1ae530)
//   svc_CmdKeyValues   vtable 0x3af590   read 0x1ae960 (-> Base read 0x1ae530)
//   net_SetConVar      vtable 0x3afb68   process 0x2eef30
//   clc_Move           vtable 0x3afe10   process 0x2ef130
//
// Message layouts match PC's: the handler at +0x18; net_SetConVar's entries
// (two 0x104-byte strings, 0x208 each, read by engine+0x1b4690) at +0x20 and
// their count at +0x38; clc_Move's backup/new command counts at +0x20/+0x24 and
// its bit length at +0x28 (engine+0x1ac460). PC tells a server's copy of a
// message from a client's by thread; on PS4 the listen server and the client
// share threads, so a message whose handler lies in the engine's client array
// (engine+0x3818680, stride 0x2d738) is the server's.
//
// ReadUsercmd and GetEntByIndex (server.prx), the LZSS rewrite, the
// WriteBaselines overflow and the NET_ReceiveDatagram limit are further down.
// Not ported: CL_CopyExistingEntity, the UTF-8 parser fix and
// CNetChan::ProcessMessages limits (ns_limits).

namespace netfixes {
using MessageFn = bool (*)(void* message);
using BufferFn = bool (*)(void* message, void* buffer);

constexpr std::size_t kHandlerOffset = 0x18;
constexpr std::size_t kSetConVarEntriesOffset = 0x20;
constexpr std::size_t kSetConVarCountOffset = 0x38;
constexpr std::size_t kSetConVarStringSize = 0x104;  // PC ENTRY_STR_LEN 260
constexpr int kSetConVarServerLimit = 69;            // PC SETCONVAR_SANITY_AMOUNT_LIMIT
constexpr std::size_t kMoveBackupOffset = 0x20;
constexpr std::size_t kMoveNewOffset = 0x24;
constexpr std::size_t kMoveLengthOffset = 0x28;
constexpr int kFcvarReplicated = 1 << 13;

std::uintptr_t g_engineBase = 0;
void* g_logConVar = nullptr;
MessageFn g_setConVarProcess = nullptr;
MessageFn g_moveProcess = nullptr;
// FindVar returns the ConVar at its ConCommandBase (name +0x18, flags +0x28).
bool g_convarLayoutChecked = false;

// Logs once per hook that the engine reaches it through the vtable.
void Active(bool& seen, const char* what) noexcept {
    if (seen) return;
    seen = true;
    LogFormat("[NorthstarPS4] netmessage fix active: %s\n", what);
}
bool g_seenSetConVarServer = false;
bool g_seenSetConVarClient = false;
bool g_seenMove = false;

bool Blocked(const char* what) noexcept {
    if (stringcommands::ConVarIntValue(g_logConVar, 1) != 0) LogFormat("[NorthstarPS4] exploit fix: blocked %s\n", what);
    return false;
}

bool IsServerMessage(const void* message) noexcept {
    const auto handler = *reinterpret_cast<const std::uintptr_t*>(static_cast<const char*>(message) + kHandlerOffset);
    const auto first = g_engineBase + kEngineClientArrayVa;
    return handler >= first && handler < first + stringcommands::kMaxTrackedClients * kEngineClientStride;
}

// "Servers can literally request a screenshot from any client, yeah no" (PC).
bool ScreenshotRead(void*, void*) noexcept { return Blocked("clc_Screenshot (received)"); }
bool ScreenshotWrite(void*, void*) noexcept { return Blocked("clc_Screenshot (sending)"); }

// Unused in game and a client=>server=>client exploit vector (PC).
bool CmdKeyValuesRead(void*, void*) noexcept { return Blocked("CmdKeyValues"); }

bool SetConVarProcess(void* message) noexcept {
    auto* const base = static_cast<char*>(message);
    const bool server = IsServerMessage(message);
    if (server)
        Active(g_seenSetConVarServer, "net_SetConVar (server)");
    else
        Active(g_seenSetConVarClient, "net_SetConVar (client)");
    const int count = *reinterpret_cast<const std::int32_t*>(base + kSetConVarCountOffset);
    auto* const entries = *reinterpret_cast<char**>(base + kSetConVarEntriesOffset);
    if (server && (count < 1 || count > kSetConVarServerLimit))
        return Blocked(count < 1 ? "net_SetConVar (server): no convars" : "net_SetConVar (server): too many convars");
    if (count < 0 || (count > 0 && !entries)) return Blocked("net_SetConVar: invalid entries");
    for (int i = 0; i < count; ++i) {
        char* const name = entries + static_cast<std::size_t>(i) * 2 * kSetConVarStringSize;
        char* const value = name + kSetConVarStringSize;
        if (!std::memchr(name, 0, kSetConVarStringSize) || !std::memchr(value, 0, kSetConVarStringSize))
            return Blocked("net_SetConVar: missing null terminators");
        // Only the client sets real ConVars from this message; the server only
        // stores the values on the player.
        if (server || !g_convarLayoutChecked) continue;
        void* const convar = g_modConVarFindVar(g_modConVarCvar, name);
        if (!convar) continue;
        const char* const canonical = *reinterpret_cast<const char* const*>(static_cast<char*>(convar) + 0x18);
        if (canonical && std::strlen(canonical) < kSetConVarStringSize)
            std::memcpy(name, canonical, std::strlen(canonical) + 1);  // force the name's case to match
        const std::int32_t flags = *reinterpret_cast<const std::int32_t*>(static_cast<char*>(convar) + 0x28);
        if (!(flags & kFcvarReplicated)) {
            // Not blocked: well-meaning servers send these too, and their
            // players should still be able to connect.
            LogFormat("[NorthstarPS4] blocking replication of %s from the server (not REPLICATED here)\n", name);
            std::memset(name, 0, kSetConVarStringSize);
            std::memset(value, 0, kSetConVarStringSize);
        }
    }
    return g_setConVarProcess(message);
}

bool MoveProcess(void* message) noexcept {
    auto* const base = static_cast<const char*>(message);
    Active(g_seenMove, "clc_Move");
    if (*reinterpret_cast<const std::int32_t*>(base + kMoveBackupOffset) < 0)
        return Blocked("clc_Move: invalid backup command count");
    if (*reinterpret_cast<const std::int32_t*>(base + kMoveNewOffset) < 0)
        return Blocked("clc_Move: invalid new command count");
    if (*reinterpret_cast<const std::int32_t*>(base + kMoveLengthOffset) <= 0)
        return Blocked("clc_Move: invalid length");
    return g_moveProcess(message);
}

// PC Cbuf_HasRoomForExecutionMarkers / CBaseClientState_ProcessStringCmd. The
// client's ProcessStringCmd (engine+0x155e80, slot 2 of the vtable at
// engine+0x3a25a0) wraps a remote server's command in two execution markers
// so that only FCVAR_SERVER_CAN_EXECUTE commands run: m_bRestrictServerCommands
// (+0xfac4) is set to 1 by the client state's constructor (engine+0x77247)
// and never cleared, so PC's IsRespawnMod hook has nothing to do here. When
// the marker list (count at engine+0x3e78fe0) is past 2048, the PS4 engine
// drops its oldest marker, and a dropped "enable" marker would let the
// commands after it run unrestricted. As on PC, a command that leaves no room
// for its two markers is ignored instead.
constexpr std::uintptr_t kExecutionMarkerCountVa = 0x3e78fe0;
constexpr int kMaxExecutionMarkers = 2048;
BufferFn g_clientStringCmd = nullptr;

bool ClientStringCmd(void* clientState, void* message) noexcept {
    if (*reinterpret_cast<const std::int32_t*>(g_engineBase + kExecutionMarkerCountVa) + 2 >= kMaxExecutionMarkers) {
        Blocked("server string command (no room for execution markers)");
        return true;  // ignored, as on PC; false would drop the connection
    }
    return g_clientStringCmd(clientState, message);
}

struct SlotHook {
    const char* message;
    std::uintptr_t vtableVa;
    int slot;
    std::uintptr_t expectedVa;
    void* hook;
    void** original;  // receives the engine function, when kept
};

bool WriteEngineData(std::uintptr_t address, std::uintptr_t value) noexcept {
    const auto page = address & ~std::uintptr_t(0x3fff);
    if (sceKernelMprotect(reinterpret_cast<void*>(page), 0x4000, 3) != 0) return false;
    *reinterpret_cast<volatile std::uintptr_t*>(address) = value;
    return sceKernelMprotect(reinterpret_cast<void*>(page), 0x4000, 1) == 0;
}
} // namespace netfixes

void InstallLzssFix(std::uintptr_t engineBase) noexcept;  // below
void InstallBaselineOverflowFix(std::uintptr_t engineBase) noexcept;  // below
void InstallDatagramLimit(std::uintptr_t engineBase) noexcept;  // below

void InstallNetMessageFixes(std::uintptr_t engineBase, std::size_t engineSize) noexcept {
    using namespace netfixes;
    (void)engineSize;
    InstallLzssFix(engineBase);
    InstallBaselineOverflowFix(engineBase);
    InstallDatagramLimit(engineBase);
    g_engineBase = engineBase;
    alignas(16) static std::uint8_t logStorage[0x90]{};
    g_logConVar = atlasserver::RegisterConVar(logStorage, "ns_exploitfixes_log", "1", "Whether to log whenever ExploitFixes.cpp blocks/corrects something");
    if (g_modConVarCvar && g_modConVarFindVar) {
        void* const hostport = g_modConVarFindVar(g_modConVarCvar, "hostport");
        g_convarLayoutChecked = hostport && hostport == stringcommands::FindCommandBase("hostport");
    }
    if (!g_convarLayoutChecked)
        LogFormat("[NorthstarPS4] net_SetConVar: ConVar layout unverified; client-side REPLICATED check off\n");
    const SlotHook hooks[] = {
        {"clc_Screenshot", 0x3b0938, 5, 0x1b86b0, reinterpret_cast<void*>(&ScreenshotRead), nullptr},
        {"clc_Screenshot", 0x3b0938, 6, 0x1b8620, reinterpret_cast<void*>(&ScreenshotWrite), nullptr},
        {"clc_CmdKeyValues", 0x3af508, 5, 0x1ae8d0, reinterpret_cast<void*>(&CmdKeyValuesRead), nullptr},
        {"svc_CmdKeyValues", 0x3af590, 5, 0x1ae960, reinterpret_cast<void*>(&CmdKeyValuesRead), nullptr},
        {"net_SetConVar", 0x3afb68, 4, 0x2eef30, reinterpret_cast<void*>(&SetConVarProcess),
            reinterpret_cast<void**>(&g_setConVarProcess)},
        {"clc_Move", 0x3afe10, 4, 0x2ef130, reinterpret_cast<void*>(&MoveProcess), reinterpret_cast<void**>(&g_moveProcess)},
        {"client ProcessStringCmd", 0x3a25a0, 2, 0x155e80, reinterpret_cast<void*>(&ClientStringCmd),
            reinterpret_cast<void**>(&g_clientStringCmd)},
    };
    // Every vtable's GetName (slot 11) must be the function that names it.
    const struct { std::uintptr_t vtableVa, getNameVa; } names[] = {
        {0x3b0938, 0x2efbd0}, {0x3af508, 0x2efe10}, {0x3af590, 0x2efe60}, {0x3afb68, 0x2eef90}, {0x3afe10, 0x2ef170}};
    for (const auto& name : names) {
        if (*reinterpret_cast<const std::uintptr_t*>(engineBase + name.vtableVa + 11 * 8) != engineBase + name.getNameVa) {
            LogFormat("[NorthstarPS4] netmessage fixes refused: engine profile mismatch (vtable %lx)\n", name.vtableVa);
            return;
        }
    }
    for (const auto& hook : hooks) {
        const auto slot = engineBase + hook.vtableVa + static_cast<std::uintptr_t>(hook.slot) * 8;
        if (*reinterpret_cast<const std::uintptr_t*>(slot) != engineBase + hook.expectedVa) {
            LogFormat("[NorthstarPS4] netmessage fixes refused: %s slot %d is not the expected function\n", hook.message,
                hook.slot);
            return;
        }
    }
    int installed = 0;
    for (const auto& hook : hooks) {
        const auto slot = engineBase + hook.vtableVa + static_cast<std::uintptr_t>(hook.slot) * 8;
        if (hook.original) *hook.original = reinterpret_cast<void*>(engineBase + hook.expectedVa);
        if (WriteEngineData(slot, reinterpret_cast<std::uintptr_t>(hook.hook)))
            ++installed;
        else
            LogFormat("[NorthstarPS4] netmessage fix not installed: %s slot %d (protection)\n", hook.message, hook.slot);
    }
    LogFormat("[NorthstarPS4] netmessage fixes installed (%d/%zu)\n", installed, sizeof(hooks) / sizeof(hooks[0]));
}

// PC GetEntByIndex (server.dll): an index of 0x4000 or more reads past the
// entity list, and one script client command takes an arbitrary index. On PS4
// the GetEntByIndex script native (server+0x71b150) raises a script error for
// a negative index but indexes the 0x4000-entry list at server+0xfc27a0
// without an upper bound: index 16384.5 read a garbage entity and crashed the
// game. The guard returns null for an index past the list, as PC's fix does.
namespace serverfixes {
constexpr std::uintptr_t kGetEntByIndexNativeVa = 0x71b150;
constexpr std::uintptr_t kGetEntByIndexLeaVa = 0x1d574e;
constexpr std::uint32_t kSqInteger = 0x5000002;
constexpr int kMaxEntityIndex = 0x4000;
using ScriptNativeFn = std::int64_t (*)(void* vm);
ScriptNativeFn g_getEntByIndex = nullptr;

std::int64_t GetEntByIndexGuard(void* vm) noexcept {
    auto* const argument = *reinterpret_cast<char**>(static_cast<char*>(vm) + 0x48);
    if (argument) {
        const bool integer = *reinterpret_cast<const std::uint32_t*>(argument + 0x10) == kSqInteger;
        // The native truncates a float; floats past int range and NaN become
        // negative there and get its script error, as a negative index does.
        const bool outOfBounds = integer ? *reinterpret_cast<const std::int32_t*>(argument + 0x18) >= kMaxEntityIndex
                                         : *reinterpret_cast<const float*>(argument + 0x18) >= kMaxEntityIndex;
        if (outOfBounds) {
            LogFormat("[NorthstarPS4] GetEntByIndex %d is out of bounds (max %d)\n",
                integer ? *reinterpret_cast<const std::int32_t*>(argument + 0x18)
                        : static_cast<int>(*reinterpret_cast<const float*>(argument + 0x18)),
                kMaxEntityIndex);
            // PC returns null; the native's own negative-index path raises a
            // script error instead, so the null is pushed here.
            uiapi::Null(vm);
            return 1;
        }
    }
    return g_getEntByIndex(vm);
}

// PC ReadUsercmd (server.dll): every command a client sends is checked after
// it is read. Invalid (non-finite) angles, movement and camera vectors are
// zeroed, and a command with bogus timing has everything that affects play
// cleared. The PS4 CPlayer::ProcessUsercmds (server+0xb5950) reads up to 64
// commands into a stack array with ReadUsercmd inlined, then passes the array
// to CBasePlayer::ProcessUsercmds (server+0x43e0f0, called at server+0xbbb15:
// player, cmds, numcmds, totalcmds, dropped, paused). That call is redirected
// here, so every command is checked before it is used. The CUserCmd layout
// matches PC's SV_CUserCmd for every field touched (command_number, tick_count
// and command_time printed by net_sv_showusercmd at +0/+4/+8; the copy at
// server+0x198a60 has PC's field widths through frameTime at +0x9c), with a
// stride of 0x138.
constexpr std::uintptr_t kProcessUsercmdsCallVa = 0xbbb15;
constexpr std::uintptr_t kProcessUsercmdsVa = 0x43e0f0;
constexpr std::size_t kUserCmdStride = 0x138;
constexpr int kMaxUserCmds = 64;
using ProcessUsercmdsFn = void (*)(void* player, char* cmds, int numcmds, int totalcmds, int dropped, int paused);
ProcessUsercmdsFn g_processUsercmds = nullptr;
std::uint64_t g_lastBogusLog = 0;

struct UserCmdFields {
    static constexpr std::size_t kTickCount = 0x04, kCommandTime = 0x08, kWorldViewAngles = 0x0c,
        kLocalViewAngles = 0x1c, kAttackAngles = 0x28, kMove = 0x34, kButtons = 0x40, kMeleeTarget = 0x48,
        kCameraPos = 0x70, kCameraAngles = 0x7c, kFrameTime = 0x9c;
};

void ResetIfInvalid(char* cmd, std::size_t offset) noexcept {
    auto* v = reinterpret_cast<float*>(cmd + offset);
    if (!__builtin_isfinite(v[0]) || !__builtin_isfinite(v[1]) || !__builtin_isfinite(v[2])) v[0] = v[1] = v[2] = 0.0f;
}
void Zero(char* cmd, std::size_t offset) noexcept {
    auto* v = reinterpret_cast<float*>(cmd + offset);
    v[0] = v[1] = v[2] = 0.0f;
}

void SanitizeUserCmd(char* cmd) noexcept {
    using F = UserCmdFields;
    ResetIfInvalid(cmd, F::kWorldViewAngles);
    ResetIfInvalid(cmd, F::kAttackAngles);
    ResetIfInvalid(cmd, F::kLocalViewAngles);
    ResetIfInvalid(cmd, F::kCameraPos);
    ResetIfInvalid(cmd, F::kCameraAngles);
    ResetIfInvalid(cmd, F::kMove);
    const float frameTime = *reinterpret_cast<const float*>(cmd + F::kFrameTime);
    const std::uint32_t tickCount = *reinterpret_cast<const std::uint32_t*>(cmd + F::kTickCount);
    const float commandTime = *reinterpret_cast<const float*>(cmd + F::kCommandTime);
    // !(x > 0) also catches NaN, which PC's `x <= 0` lets through.
    if (!(frameTime > 0.0f) || tickCount == 0 || !(commandTime > 0.0f)) {
        // PC logs every one; once every 5 s is enough to see it happening.
        const std::uint64_t now = sceKernelGetProcessTime();
        if (now - g_lastBogusLog >= 5000000) {
            g_lastBogusLog = now;
            if (stringcommands::ConVarIntValue(netfixes::g_logConVar, 1) != 0)
                LogFormat("[NorthstarPS4] exploit fix: ReadUsercmd: bogus cmd timing (tick_count: %u, frameTime: %f, "
                          "commandTime: %f)\n", tickCount, static_cast<double>(frameTime), static_cast<double>(commandTime));
        }
        Zero(cmd, F::kWorldViewAngles);
        Zero(cmd, F::kLocalViewAngles);
        Zero(cmd, F::kAttackAngles);
        Zero(cmd, F::kCameraAngles);
        Zero(cmd, F::kMove);
        Zero(cmd, F::kCameraPos);
        *reinterpret_cast<std::uint32_t*>(cmd + F::kTickCount) = 0;
        *reinterpret_cast<float*>(cmd + F::kFrameTime) = 0.0f;
        *reinterpret_cast<std::uint32_t*>(cmd + F::kButtons) = 0;
        *reinterpret_cast<std::uint32_t*>(cmd + F::kMeleeTarget) = 0;
    }
}

void ProcessUsercmdsGuard(void* player, char* cmds, int numcmds, int totalcmds, int dropped, int paused) noexcept {
    const int count = totalcmds < 0 ? 0 : (totalcmds > kMaxUserCmds ? kMaxUserCmds : totalcmds);
    if (cmds)
        for (int i = 0; i < count; ++i) SanitizeUserCmd(cmds + static_cast<std::size_t>(i) * kUserCmdStride);
    g_processUsercmds(player, cmds, numcmds, totalcmds, dropped, paused);
}
} // namespace serverfixes

// The binding is filled in on every SERVER VM creation (server+0x1d5600 writes
// the native's address into it just before registering it), so the guard goes
// into that code instead: `lea rdi, [rip+disp]` at server+0x1d574e loads
// server+0x71b150, and its displacement is pointed at the guard. Installed
// once server.prx is mapped, before any SERVER VM is created.
namespace serverfixes {
void InstallGetEntByIndexGuard(std::uintptr_t serverBase) noexcept {
    // lea rdi, [rip+0x5459fb]  ->  server+0x71b150
    constexpr std::uint8_t leaBytes[] = {0x48, 0x8d, 0x3d, 0xfb, 0x59, 0x54, 0x00};
    if (std::memcmp(reinterpret_cast<const void*>(serverBase + kGetEntByIndexLeaVa), leaBytes, sizeof(leaBytes)) != 0 ||
        kGetEntByIndexLeaVa + sizeof(leaBytes) + 0x5459fb != kGetEntByIndexNativeVa) {
        LogFormat("[NorthstarPS4] GetEntByIndex guard refused: server profile mismatch\n");
        return;
    }
    const auto next = static_cast<std::int64_t>(serverBase + kGetEntByIndexLeaVa + sizeof(leaBytes));
    const auto relative = static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(&GetEntByIndexGuard)) - next;
    if (relative < -2147483648LL || relative > 2147483647LL) {
        LogFormat("[NorthstarPS4] GetEntByIndex guard refused: guard outside rel32 range\n");
        return;
    }
    g_getEntByIndex = reinterpret_cast<ScriptNativeFn>(serverBase + kGetEntByIndexNativeVa);
    const auto displacement = static_cast<std::int32_t>(relative);
    if (!WriteEngineCode(serverBase + kGetEntByIndexLeaVa + 3, &displacement, sizeof(displacement))) {
        LogFormat("[NorthstarPS4] GetEntByIndex guard: write failed\n");
        return;
    }
    LogFormat("[NorthstarPS4] GetEntByIndex guard installed\n");
}

void InstallUsercmdChecks(std::uintptr_t serverBase) noexcept {
    // call CBasePlayer::ProcessUsercmds (rel32 to server+0x43e0f0), and that
    // function's prologue.
    constexpr std::uint8_t callBytes[] = {0xe8, 0xd6, 0x25, 0x38, 0x00};
    constexpr std::uint8_t targetBytes[] = {0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x53,
        0x48, 0x83, 0xec, 0x38};
    if (std::memcmp(reinterpret_cast<const void*>(serverBase + kProcessUsercmdsCallVa), callBytes, sizeof(callBytes)) != 0 ||
        std::memcmp(reinterpret_cast<const void*>(serverBase + kProcessUsercmdsVa), targetBytes, sizeof(targetBytes)) != 0) {
        LogFormat("[NorthstarPS4] usercmd checks refused: server profile mismatch\n");
        return;
    }
    const auto callNext = static_cast<std::int64_t>(serverBase + kProcessUsercmdsCallVa + 5);
    const auto callRelative = static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(&ProcessUsercmdsGuard)) - callNext;
    if (callRelative < -2147483648LL || callRelative > 2147483647LL) {
        LogFormat("[NorthstarPS4] usercmd checks refused: guard outside rel32 range\n");
        return;
    }
    g_processUsercmds = reinterpret_cast<ProcessUsercmdsFn>(serverBase + kProcessUsercmdsVa);
    const auto callDisplacement = static_cast<std::int32_t>(callRelative);
    if (!WriteEngineCode(serverBase + kProcessUsercmdsCallVa + 1, &callDisplacement, sizeof(callDisplacement)))
        LogFormat("[NorthstarPS4] usercmd checks: write failed\n");
    else
        LogFormat("[NorthstarPS4] usercmd checks installed\n");
}
} // namespace serverfixes

void InstallServerUnsafeFuncStubs(std::uintptr_t serverBase) noexcept;  // below

// Once server.prx is mapped, before any SERVER VM is created.
void InstallServerExploitFixes(std::uintptr_t serverBase) noexcept {
    static bool done = false;
    if (!serverBase || done) return;
    done = true;
    serverfixes::InstallGetEntByIndexGuard(serverBase);
    serverfixes::InstallUsercmdChecks(serverBase);
    InstallServerUnsafeFuncStubs(serverBase);
}

// PC squirrel.cpp StubUnsafeSQFuncs: unless -allowunsafesqfuncs is given,
// DevTextBufferWrite, DevTextBufferClear, DevTextBufferDumpToFile,
// Dev_CommandLineAddParm, DevP4Checkout and DevP4Add are replaced by a stub
// that logs "Blocking call to stubbed function" and returns null, so a mod
// script cannot write files or add command-line parameters. All six are still
// registered in the retail PS4 client.prx (UI and CLIENT) and server.prx. Each
// module fills a ScriptFunctionBinding per native on every VM creation and
// loads the native's address with `lea rdi|rax, [rip+native]` just before
// storing it at binding+0x60 (as for GetEntByIndex), so each of those 12
// displacements is pointed at the stub.
namespace unsafefuncs {
constexpr const char* kNames[] = {"DevTextBufferWrite", "DevTextBufferClear", "DevTextBufferDumpToFile",
    "Dev_CommandLineAddParm", "DevP4Checkout", "DevP4Add"};
struct Site { int name; std::uintptr_t leaVa; std::uint8_t bytes[7]; std::uintptr_t nativeVa; };
constexpr Site kClientSites[] = {
    {0, 0x30f96c, {0x48, 0x8d, 0x3d, 0x4d, 0xb7, 0x46, 0x00}, 0x77b0c0},
    {1, 0x30fa97, {0x48, 0x8d, 0x05, 0xd2, 0xb7, 0x46, 0x00}, 0x77b270},
    {2, 0x30fc83, {0x48, 0x8d, 0x3d, 0x06, 0xb7, 0x46, 0x00}, 0x77b390},
    {3, 0x30ada8, {0x48, 0x8d, 0x05, 0x61, 0xc5, 0x46, 0x00}, 0x777310},
    {4, 0x30f58a, {0x48, 0x8d, 0x3d, 0x0f, 0xba, 0x46, 0x00}, 0x77afa0},
    {5, 0x30f77b, {0x48, 0x8d, 0x3d, 0xae, 0xb8, 0x46, 0x00}, 0x77b030},
};
constexpr Site kServerSites[] = {
    {0, 0x1dd821, {0x48, 0x8d, 0x3d, 0xf8, 0x4a, 0x54, 0x00}, 0x722320},
    {1, 0x1dd94c, {0x48, 0x8d, 0x05, 0x7d, 0x4a, 0x54, 0x00}, 0x7223d0},
    {2, 0x1ddb38, {0x48, 0x8d, 0x3d, 0xb1, 0x49, 0x54, 0x00}, 0x7224f0},
    {3, 0x1d8c5d, {0x48, 0x8d, 0x05, 0x1c, 0x59, 0x54, 0x00}, 0x71e580},
    {4, 0x1dd43f, {0x48, 0x8d, 0x3d, 0xba, 0x4d, 0x54, 0x00}, 0x722200},
    {5, 0x1dd630, {0x48, 0x8d, 0x3d, 0x59, 0x4c, 0x54, 0x00}, 0x722290},
};

template <int Name, bool Server> std::int64_t Stub(void*) noexcept {
    LogFormat("[NorthstarPS4] Blocking call to stubbed function %s in %s\n", kNames[Name], Server ? "SERVER" : "UI/CLIENT");
    return 0;  // no return value: the script gets null, as PC's SQRESULT_NULL
}
template <bool Server> void* StubFor(int name) noexcept {
    switch (name) {
        case 0: return reinterpret_cast<void*>(&Stub<0, Server>);
        case 1: return reinterpret_cast<void*>(&Stub<1, Server>);
        case 2: return reinterpret_cast<void*>(&Stub<2, Server>);
        case 3: return reinterpret_cast<void*>(&Stub<3, Server>);
        case 4: return reinterpret_cast<void*>(&Stub<4, Server>);
        default: return reinterpret_cast<void*>(&Stub<5, Server>);
    }
}

template <bool Server, std::size_t N> void Install(std::uintptr_t base, const Site (&sites)[N], const char* module) noexcept {
    if (StartupArgPresent("-allowunsafesqfuncs")) {
        LogFormat("[NorthstarPS4] unsafe script functions left enabled in %s (-allowunsafesqfuncs)\n", module);
        return;
    }
    for (const auto& site : sites) {
        std::int32_t displacement = 0;
        std::memcpy(&displacement, site.bytes + 3, sizeof(displacement));
        if (std::memcmp(reinterpret_cast<const void*>(base + site.leaVa), site.bytes, sizeof(site.bytes)) != 0 ||
            static_cast<std::int64_t>(site.leaVa + 7) + displacement != static_cast<std::int64_t>(site.nativeVa)) {
            LogFormat("[NorthstarPS4] unsafe script functions not stubbed in %s: profile mismatch at %lx\n", module, site.leaVa);
            return;
        }
    }
    int stubbed = 0;
    for (const auto& site : sites) {
        const auto relative = static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(StubFor<Server>(site.name))) -
            static_cast<std::int64_t>(base + site.leaVa + 7);
        if (relative < -2147483648LL || relative > 2147483647LL) continue;
        const auto value = static_cast<std::int32_t>(relative);
        if (WriteEngineCode(base + site.leaVa + 3, &value, sizeof(value))) ++stubbed;
    }
    LogFormat("[NorthstarPS4] unsafe script functions stubbed in %s (%d/%zu)\n", module, stubbed, N);
}
} // namespace unsafefuncs

void InstallClientUnsafeFuncStubs(std::uintptr_t clientBase) noexcept {
    if (clientBase) unsafefuncs::Install<false>(clientBase, unsafefuncs::kClientSites, "client.prx");
}
void InstallServerUnsafeFuncStubs(std::uintptr_t serverBase) noexcept {
    if (serverBase) unsafefuncs::Install<true>(serverBase, unsafefuncs::kServerSites, "server.prx");
}

// PC exploitfixes_lzss.cpp: CLZSS::SafeUncompress rewritten so a malformed
// compressed payload cannot make it copy from before the start of the output.
// The PS4 engine's copy (engine+0x20f190; static, input in rdi, output in rsi,
// buffer size in edx) checks the header, the declared size and the output
// bound, but not that a back-reference lies within what has been written. Its
// entry is replaced by a jump to this rewrite, which is PC's, so the original
// is never run.
namespace lzssfix {
constexpr std::uintptr_t kSafeUncompressVa = 0x20f190;

// The engine's calling convention: input, output, buffer size (no `this`).
unsigned int SafeUncompress(const unsigned char* input, unsigned char* output, unsigned int bufferSize) noexcept {
    return lzss::SafeUncompress(input, output, bufferSize);
}
} // namespace lzssfix

void InstallLzssFix(std::uintptr_t engineBase) noexcept {
    using namespace lzssfix;
    // push rbp; push r14; push rbx; xor eax, eax; test rdi, rdi; je ...;
    // cmp dword [rdi], "LZSS"
    constexpr std::uint8_t entry[] = {0x55, 0x41, 0x56, 0x53, 0x31, 0xc0, 0x48, 0x85, 0xff, 0x0f, 0x84, 0xd5, 0x00, 0x00,
        0x00, 0x81, 0x3f, 0x4c, 0x5a, 0x53, 0x53};
    const auto function = engineBase + kSafeUncompressVa;
    if (std::memcmp(reinterpret_cast<const void*>(function), entry, sizeof(entry)) != 0) {
        LogFormat("[NorthstarPS4] LZSS fix refused: engine profile mismatch\n");
        return;
    }
    const auto relative = static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(&SafeUncompress)) -
        static_cast<std::int64_t>(function + 5);
    if (relative < -2147483648LL || relative > 2147483647LL) {
        LogFormat("[NorthstarPS4] LZSS fix refused: rewrite outside rel32 range\n");
        return;
    }
    std::uint8_t jump[5] = {0xe9};
    const auto displacement = static_cast<std::int32_t>(relative);
    std::memcpy(jump + 1, &displacement, sizeof(displacement));
    if (WriteEngineCode(function, jump, sizeof(jump)))
        LogFormat("[NorthstarPS4] LZSS fix installed\n");
    else
        LogFormat("[NorthstarPS4] LZSS fix: write failed\n");
}

// PC exploitfixes.cpp: a client can make the server overflow
// CNetworkStringTableContainer::WriteBaselines while it is sent its server
// info. PC patches the failure to set a flag and disconnects that client when
// CBaseClient::SendServerInfo returns, instead of the server stopping. On PS4
// SendServerInfo is inlined into the server's per-client frame code
// (engine+0xe81a0), and the index and overflow errors share one
// `call Host_Error` (engine+0x10d560) at engine+0xe997d. That call goes to
// ns_baseline_overflow_thunk (runtime.cpp), which passes on the client, and
// the code carries on as it does after a successful write. The client is
// disconnected with PC's message after the host frame.
namespace baselinefix {
constexpr std::uintptr_t kOverflowSiteVa = 0xe9973;
constexpr std::uintptr_t kHostErrorCallVa = 0xe997d;
constexpr std::uintptr_t kOverflowStringVa = 0x3460a3;
std::uintptr_t g_engine = 0;
std::atomic<std::uint64_t> g_pending{0};

void Failed(const char* format, const char* table, long clientOffset) noexcept {
    (void)format;
    const long count = *reinterpret_cast<const std::int32_t*>(g_engine + kEngineClientCountVa);
    const long stride = static_cast<long>(kEngineClientStride);
    if (clientOffset < 0 || clientOffset % stride != 0 || clientOffset / stride >= count || clientOffset / stride >= 64) {
        LogFormat("[NorthstarPS4] exploit fix: WriteBaselines failed (%s), client unknown\n", table ? table : "?");
        return;
    }
    const int client = static_cast<int>(clientOffset / stride);
    g_pending.fetch_or(std::uint64_t(1) << client);
    LogFormat("[NorthstarPS4] exploit fix: WriteBaselines failed for client #%d (%s); disconnecting it\n", client,
        table ? table : "?");
}
} // namespace baselinefix

// From the host frame, after the frame that wrote the baselines.
void DisconnectBaselineOverflows() noexcept {
    std::uint64_t pending = baselinefix::g_pending.exchange(0);
    while (pending) {
        const int client = __builtin_ctzll(pending);
        pending &= pending - 1;
        if (!g_disconnectClient || !g_disconnectClient(client,
                "Overflowed CNetworkStringTableContainer::WriteBaselines, try restarting your client and reconnecting"))
            LogFormat("[NorthstarPS4] WriteBaselines: could not disconnect client #%d\n", client);
    }
}

void InstallBaselineOverflowFix(std::uintptr_t engineBase) noexcept {
    using namespace baselinefix;
    // lea rdi, [rip+0x25c729] (the overflow message); mov rsi, rcx;
    // call Host_Error
    constexpr std::uint8_t site[] = {0x48, 0x8d, 0x3d, 0x29, 0xc7, 0x25, 0x00, 0x48, 0x89, 0xce, 0xe8, 0xde, 0x3b, 0x02,
        0x00};
    constexpr char message[] = "Overflow error writing string table baseline %s\n";
    if (std::memcmp(reinterpret_cast<const void*>(engineBase + kOverflowSiteVa), site, sizeof(site)) != 0 ||
        std::memcmp(reinterpret_cast<const void*>(engineBase + kOverflowStringVa), message, sizeof(message)) != 0) {
        LogFormat("[NorthstarPS4] WriteBaselines fix refused: engine profile mismatch\n");
        return;
    }
    const auto relative = static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(&ns_baseline_overflow_thunk)) -
        static_cast<std::int64_t>(engineBase + kHostErrorCallVa + 5);
    if (relative < -2147483648LL || relative > 2147483647LL) {
        LogFormat("[NorthstarPS4] WriteBaselines fix refused: thunk outside rel32 range\n");
        return;
    }
    g_engine = engineBase;
    ns_baseline_overflow_target = &Failed;
    const auto displacement = static_cast<std::int32_t>(relative);
    if (WriteEngineCode(engineBase + kHostErrorCallVa + 1, &displacement, sizeof(displacement)))
        LogFormat("[NorthstarPS4] WriteBaselines fix installed\n");
    else
        LogFormat("[NorthstarPS4] WriteBaselines fix: write failed\n");
}

// PC exploitfixes.cpp NET_ReceiveDatagram (from R1Delta): a packet that fails
// to decode (an invalid LZSS payload, say) makes the receive return nothing,
// and the socket loop stops reading for that frame, so one such packet per
// frame starves everyone else's. PC retries up to ns_recvfrom_per_frame_limit
// times while the socket had data.
//
// On PS4, NET_ProcessSocket (engine+0x1a6350) calls NET_GetPacket
// (engine+0x1a5500, NET_ReceiveDatagram inlined) at engine+0x1a6442 and
// engine+0x1a853c. NET_GetPacket's recvfrom call (engine+0x1a596e, to the PLT
// at engine+0x1dd8) treats 0 or EAGAIN/EMSGSIZE as no data, and there is no
// net_error to read. So both calls go through a guard that retries only when
// recvfrom returned data on that thread and still no packet came out.
// NET_ProcessSocket takes its scratch buffers from a lock-free list, so the
// state is kept per thread (keyed by %fs:0, the thread's self pointer).
namespace datagramfix {
constexpr std::uintptr_t kRecvFromCallVa = 0x1a596e;
constexpr std::uintptr_t kRecvFromLengthVa = 0x1a594f;
constexpr std::uintptr_t kRecvFromPltVa = 0x1dd8;
constexpr std::uintptr_t kGetPacketVa = 0x1a5500;
constexpr std::uintptr_t kGetPacketCallVas[] = {0x1a6442, 0x1a853c};
using RecvFromFn = long (*)(int fd, void* buffer, unsigned long length, int flags, void* from, void* fromLength);
using GetPacketFn = void* (*)(int socket, void* scratch, int encrypted);
RecvFromFn g_recvFrom = nullptr;
GetPacketFn g_getPacket = nullptr;
void* g_limit = nullptr;

struct ThreadState {
    std::atomic<std::uintptr_t> thread;
    bool received;
};
ThreadState g_threads[16];

ThreadState* CurrentThreadState() noexcept {
    std::uintptr_t self;
    asm volatile("movq %%fs:0, %0" : "=r"(self));
    for (auto& state : g_threads)
        if (state.thread.load(std::memory_order_relaxed) == self) return &state;
    for (auto& state : g_threads) {
        std::uintptr_t empty = 0;
        if (state.thread.compare_exchange_strong(empty, self)) return &state;
    }
    return nullptr;
}

// errno is left as recvfrom set it; the engine reads it next.
long RecvFromGuard(int fd, void* buffer, unsigned long length, int flags, void* from, void* fromLength) noexcept {
    const long result = g_recvFrom(fd, buffer, length, flags, from, fromLength);
    if (result > 0)
        if (ThreadState* const state = CurrentThreadState()) state->received = true;
    return result;
}

void* GetPacketGuard(int socket, void* scratch, int encrypted) noexcept {
    ThreadState* const state = CurrentThreadState();
    int limit = stringcommands::ConVarIntValue(g_limit, 1000);
    if (limit < 1) limit = 1;
    for (int i = 0; i < limit; ++i) {
        if (state) state->received = false;
        if (void* const packet = g_getPacket(socket, scratch, encrypted)) return packet;
        if (!state || !state->received) break;
    }
    return nullptr;
}

bool CallTargets(std::uintptr_t engineBase, std::uintptr_t callVa, std::uintptr_t targetVa) noexcept {
    const auto* call = reinterpret_cast<const std::uint8_t*>(engineBase + callVa);
    std::int32_t displacement;
    std::memcpy(&displacement, call + 1, sizeof(displacement));
    return call[0] == 0xe8 && callVa + 5 + displacement == targetVa;
}

bool Redirect(std::uintptr_t engineBase, std::uintptr_t callVa, const void* guard) noexcept {
    const auto relative = static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(guard)) -
        static_cast<std::int64_t>(engineBase + callVa + 5);
    if (relative < -2147483648LL || relative > 2147483647LL) return false;
    const auto displacement = static_cast<std::int32_t>(relative);
    return WriteEngineCode(engineBase + callVa + 1, &displacement, sizeof(displacement));
}
} // namespace datagramfix

void InstallDatagramLimit(std::uintptr_t engineBase) noexcept {
    using namespace datagramfix;
    // NET_GetPacket's prologue (sub rsp, 0x15b8), and `mov edx, 0x4f0` (the
    // receive length) in front of its recvfrom call.
    constexpr std::uint8_t prologue[] = {0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x53,
        0x48, 0x81, 0xec, 0xb8, 0x15, 0x00, 0x00};
    constexpr std::uint8_t length[] = {0xba, 0xf0, 0x04, 0x00, 0x00};
    bool matches =
        std::memcmp(reinterpret_cast<const void*>(engineBase + kGetPacketVa), prologue, sizeof(prologue)) == 0 &&
        std::memcmp(reinterpret_cast<const void*>(engineBase + kRecvFromLengthVa), length, sizeof(length)) == 0 &&
        CallTargets(engineBase, kRecvFromCallVa, kRecvFromPltVa);
    for (const auto callVa : kGetPacketCallVas) matches = matches && CallTargets(engineBase, callVa, kGetPacketVa);
    if (!matches) {
        LogFormat("[NorthstarPS4] NET_ReceiveDatagram limit refused: engine profile mismatch\n");
        return;
    }
    alignas(16) static std::uint8_t limitStorage[0x90]{};
    g_limit = atlasserver::RegisterConVar(limitStorage, "ns_recvfrom_per_frame_limit", "1000",
        "Maximum number of recvfrom calls to process per frame");
    g_recvFrom = reinterpret_cast<RecvFromFn>(engineBase + kRecvFromPltVa);
    g_getPacket = reinterpret_cast<GetPacketFn>(engineBase + kGetPacketVa);
    // The recvfrom wrapper first, so a guard never reads a flag nothing sets.
    bool installed = Redirect(engineBase, kRecvFromCallVa, reinterpret_cast<const void*>(&RecvFromGuard));
    for (const auto callVa : kGetPacketCallVas)
        installed = installed && Redirect(engineBase, callVa, reinterpret_cast<const void*>(&GetPacketGuard));
    LogFormat(installed ? "[NorthstarPS4] NET_ReceiveDatagram limit installed\n"
                        : "[NorthstarPS4] NET_ReceiveDatagram limit: write failed\n");
}
