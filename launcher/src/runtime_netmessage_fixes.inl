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
// Not ported here: ReadUsercmd (server.prx), CL_CopyExistingEntity,
// GetEntByIndex, the WriteBaselines overflow, NET_ReceiveDatagram, the LZSS
// and UTF-8 parser fixes, and CNetChan::ProcessMessages limits (ns_limits).

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

void InstallNetMessageFixes(std::uintptr_t engineBase, std::size_t engineSize) noexcept {
    using namespace netfixes;
    (void)engineSize;
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
} // namespace serverfixes

// The binding is filled in on every SERVER VM creation (server+0x1d5600 writes
// the native's address into it just before registering it), so the guard goes
// into that code instead: `lea rdi, [rip+disp]` at server+0x1d574e loads
// server+0x71b150, and its displacement is pointed at the guard. Installed
// once server.prx is mapped, before any SERVER VM is created.
void InstallServerExploitFixes(std::uintptr_t serverBase) noexcept {
    using namespace serverfixes;
    static bool done = false;
    if (!serverBase || done) return;
    done = true;
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
