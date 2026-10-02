// SERVER script VM constants.
//
// Starting the lobby spins up a local SERVER VM, and that VM had never been
// touched by this module - `server.prx` was neither profiled nor hooked. The
// result was a hard stop as soon as `map mp_lobby` ran:
//
//   FatalError: _items.nut: SERVER SCRIPT COMPILE ERROR: Undefined variable "VANILLA"
//
// `VANILLA` is a compile-time define, not a script global. NorthstarLauncher
// sets it with `defconst(vm, "VANILLA", ...)` in `VMCreated`, which runs for
// *every* context. This module already does the equivalent for UI and CLIENT
// when it hooks client.prx's VM initializer, so the SERVER VM was the only one
// compiling Northstar scripts without the constants they are written against.
//
// **Finding the server equivalents.** client.prx and server.prx embed the same
// Squirrel implementation, so each address was located by matching the client
// function's own bytes, choosing a stretch of the body with no rip-relative
// operands (those differ per module and defeat a naive match). Every signature
// below matched exactly once in server.prx:
//
//   VM initializer   client 0x6746c0 -> server 0x625da0   (prologue identical)
//   its call site    client 0x6717af -> server 0x622e8b
//   SQString::Create client 0x6a96a0 -> server 0x666600
//   table insert     client 0x6ab3e0 -> server 0x668470
//   const-table gate client 0x6759d2 -> server 0x6270b2
//
// The call sites corroborate each other: client's is `e8 0c 2f 00 00` and
// server's `e8 10 2f 00 00` - the same instruction reaching the same function
// from nearly the same distance, which is what the same code compiled into two
// modules looks like.
//
// The script print sink is installed per SERVER VM in RuntimeServerVmInit,
// with server.prx's own `print` addresses, so SERVER output reaches the log.
constexpr std::uintptr_t kServerVmInitVa = 0x625da0;
constexpr std::uintptr_t kServerVmInitCallVa = 0x622e8b;
constexpr std::uintptr_t kServerInternVa = 0x666600;
constexpr std::uintptr_t kServerInsertVa = 0x668470;
constexpr std::uintptr_t kServerConstTableVa = 0x6270b2;
constexpr std::uintptr_t kServerScriptOwnerContextOffset = 0x3c;

std::uintptr_t g_runtimeServerBase = 0;
std::size_t g_runtimeServerSpan = 0;
bool g_runtimeServerVmHooked = false;

// Needs the two globals above, and is called from RuntimeServerVmInit below.
#include "runtime_persistence.inl"

// Mirrors RegisterRuntimeConstants, against server.prx rather than client.prx.
// Kept separate rather than parameterised: the two share no addresses and the
// client version also registers natives and lifecycle hooks that have no
// server equivalent yet.
bool RegisterServerConstants(void* owner) noexcept {
    const auto base = g_runtimeServerBase;
    void* vm = owner ? *reinterpret_cast<void**>(reinterpret_cast<char*>(owner) + 8) : nullptr;
    void* shared = vm ? *reinterpret_cast<void**>(reinterpret_cast<char*>(vm) + 0x50) : nullptr;
    if (!shared) {
        LogFormat("[NorthstarPS4] SERVER constants not ready owner=%p vm=%p\n", owner, vm);
        return false;
    }
    void* strings = *reinterpret_cast<void**>(reinterpret_cast<char*>(shared) + 0x4048);
    void* constants = *reinterpret_cast<void**>(reinterpret_cast<char*>(shared) + 0x40e0);
    const auto tag = *reinterpret_cast<std::uint32_t*>(reinterpret_cast<char*>(shared) + 0x40d8);
    LogFormat("[NorthstarPS4] SERVER constants table vm=%p shared=%p table=%p tag=%x\n",
        vm, shared, constants, tag);
    if (!strings || !constants || tag != 0xa000020) return false;

    using InternFn = void* (*)(void*, const char*, std::int32_t);
    using InsertFn = bool (*)(void*, const void*, const void*);
    auto intern = reinterpret_cast<InternFn>(base + kServerInternVa);
    auto insert = reinterpret_cast<InsertFn>(base + kServerInsertVa);
    // The same set the client contexts get, so a script compiled for either
    // sees the same definitions.
    for (const auto& constant : ScriptConstants("SERVER")) {
        const struct { const char* name; std::int64_t value; } entry = {constant.first.c_str(), constant.second};
        void* keyString = intern(strings, entry.name, -1);
        if (!keyString) return false;
        // Same ownership fix the client path needs: SQString::Create writes
        // neither the shared-state back-pointer at +0x18 nor a reference, and
        // the table's release path at VM teardown dereferences both. A server
        // VM is destroyed on every map change, so getting this wrong here
        // would fault far more often than it did on the client.
        *reinterpret_cast<void**>(static_cast<char*>(keyString) + 0x18) = shared;
        ++*reinterpret_cast<std::uint32_t*>(static_cast<char*>(keyString) + 8);
        const std::uint64_t key[2] = {0x8000010, reinterpret_cast<std::uintptr_t>(keyString)};
        const std::uint64_t value[2] = {0x5000002, static_cast<std::uint64_t>(entry.value)};
        // 1 for a new slot, 0 when the key landed in an existing node - including
        // after a rehash, so 0 is not a failure (see TableStoreTop).
        const int result = insert(constants, key, value);
        LogFormat("[NorthstarPS4] SERVER constant name=%s value=%lld result=%d\n",
            entry.name, static_cast<long long>(entry.value), result);
    }
    return true;
}

// server.prx's copy of the native registrar, found the same way as the rest of
// this file: the client function's 17-byte prologue `55 48 89 e5 41 57 41 56
// 41 55 41 54 53 48 83 ec 68` matched exactly one address in server.prx.
constexpr std::uintptr_t kServerRegisterSquirrelFuncVa = 0x62b740;
// server.prx counterpart of client.prx's 0x6783f0 compilefile wrapper. The
// bodies are instruction-for-instruction equivalents apart from module-local
// RIP displacements.
constexpr std::uintptr_t kServerCompileFileVa = 0x629ad0;

// server.prx's copy of the Squirrel `print` native and the instruction inside
// it that loads SQSharedState::_printfunc. Found by scanning for the sink
// sequence the client gate already uses, which occurs exactly once per
// module; the 0x54 gap between the two matches the client pair exactly.
constexpr std::uintptr_t kServerScriptPrintVa = 0x691140;
constexpr std::uintptr_t kServerScriptPrintSinkVa = 0x691194;

// Mirrors RegisterRuntimeUiNatives for the SERVER VM. It walks the same
// registration table so a native marked kCtxAll is defined identically in all
// three contexts, and adds the SERVER-only entries the table now carries.
//
// Native bodies live in this PRX, but every Squirrel API call is dispatched to
// the copy in the module that owns the VM. Several helpers reach module-local
// allocator/global slots even though they take the VM as their first argument,
// so calling client.prx's copies for a SERVER VM is not safe parity.
bool RegisterServerNatives(void* owner, bool deferred = false) noexcept {
    const std::uint8_t prologue[] = {
        0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56,
        0x41, 0x55, 0x41, 0x54, 0x53, 0x48, 0x83, 0xec, 0x68,
    };
    if (!ValidateEnginePreimage(g_runtimeServerBase, g_runtimeServerSpan,
            kServerRegisterSquirrelFuncVa, prologue, sizeof(prologue))) {
        LogFormat("[NorthstarPS4] SERVER registrar mismatch va=%lx\n", kServerRegisterSquirrelFuncVa);
        return false;
    }
    // The engine keeps the record pointer rather than copying it, so the
    // records have to outlive the call. A server VM is torn down and rebuilt on
    // every map change; reusing one static block across those rebuilds is
    // deliberate, because the previous VM no longer holds the pointers.
    constexpr std::size_t kCount = sizeof(uiapi::registrations) / sizeof(uiapi::registrations[0]);
    alignas(16) static std::uint8_t records[kCount][0x68]{};
    auto registrar = reinterpret_cast<void (*)(void*, void*, void*, int, int)>(
        g_runtimeServerBase + kServerRegisterSquirrelFuncVa);

    std::size_t i = 0, registered = 0;
    for (const auto& r : uiapi::registrations) {
        auto record = records[i++];
        if (!(r.contexts & uiapi::kCtxServer)) continue;
        // PS4 resolves an unknown return type to `var` rather than repairing
        // the signature after the struct is declared. Match the client path:
        // untyped natives before InitScripts, typed natives immediately after.
        const bool needsTypes = std::strstr(r.returns, "ModInfo") ||
            std::strstr(r.returns, "ServerInfo") ||
            std::strstr(r.returns, "AuthResult") ||
            std::strstr(r.returns, "ModInstallState");
        if (needsTypes != deferred) continue;
        *reinterpret_cast<const char**>(record) = r.name;
        *reinterpret_cast<const char**>(record + 8) = r.name;
        *reinterpret_cast<const char**>(record + 0x10) = "Northstar PS4 runtime";
        *reinterpret_cast<const char**>(record + 0x18) = r.returns;
        *reinterpret_cast<const char**>(record + 0x20) = r.args;
        *reinterpret_cast<const void**>(record + 0x60) = reinterpret_cast<const void*>(r.function);
        registrar(owner, record, nullptr, 1, 0);
        ++registered;
    }
    LogFormat("[NorthstarPS4] SERVER natives registered deferred=%d count=%zu\n",
        deferred ? 1 : 0, registered);
    return true;
}

// PC's SERVER custom callbacks surround CodeCallback_MapSpawn. Omitting
// this dispatch skips SvLoadoutsMP_Init, leaving default loadouts and the
// EndUpdateCachedLoadouts signal uninitialized before player connections.
constexpr std::uintptr_t kServerMapSpawnCallVa = 0x70cd64;
constexpr std::uintptr_t kServerInitCallbackVa = 0x62b1b0;
constexpr std::uintptr_t kServerVmReleaseVa = 0x629e80;
std::vector<ScriptCallback> g_runtimeServerCallbackList;
VmLifecycle g_runtimeServerLifecycle{"SERVER", "ServerCallback", nullptr, false, &g_runtimeServerCallbackList};

// Needs the SERVER VM's lifecycle state above.
#include "runtime_chat.inl"

bool RuntimeServerMapSpawn(void* owner, const char* callback) noexcept {
    auto original = reinterpret_cast<bool (*)(void*, const char*)>(g_runtimeServerBase + kServerInitCallbackVa);
    auto& state = g_runtimeServerLifecycle;
    if (!callback || std::strcmp(callback, "CodeCallback_MapSpawn") ||
        state.owner != owner || state.started) return original(owner, callback);
    state.started = true; // Re-entry must not run the mod initialization twice.
    const bool result = DispatchScriptInitCallbacks(*state.callbacks, callback,
        [&](const char* name, const char* phase) {
            LogFormat("[NorthstarPS4] SERVER %s: %s\n", phase, name);
            const bool found = original(owner, name);
            if (!found && std::strcmp(phase, "Original"))
                LogFormat("[NorthstarPS4] SERVER %s callback not found: %s\n", phase, name);
            return found;
        });
    LogFormat("[NorthstarPS4] SERVER lifecycle completed result=%d\n", result);
    return result;
}

void InstallServerExploitFixes(std::uintptr_t serverBase) noexcept;  // runtime_netmessage_fixes.inl

bool RuntimeServerVmInit(void* owner, int context, float time) noexcept {
    using Init = bool (*)(void*, int, float);
    const bool result = reinterpret_cast<Init>(g_runtimeServerBase + kServerVmInitVa)(owner, context, time);
    LogFormat("[NorthstarPS4] SERVER VM initialized context=%d owner=%p result=%d\n",
        context, owner, result);
    // Only the SERVER context comes through server.prx, but the argument is
    // checked rather than assumed.
    if (context != 0 || !result || !owner) return result;
    uiapi::ResetAsyncCalls(context, "VM created");
    uiapi::BindSquirrelHelpers(owner, context, g_runtimeServerBase);
    // PC writes remote players' pdata when they reconnect for the next map;
    // here the slots (and their saves) carry over, so write them now.
    WriteAllRemoteSaves("map change");
    // Reset on every VM creation, including allocator reuse of the same owner.
    g_runtimeServerLifecycle.owner = owner;
    g_runtimeServerLifecycle.started = false;
    if (!LoadLifecycleCallbacks(g_runtimeServerLifecycle)) {
        LogFormat("[NorthstarPS4] SERVER callback metadata load failed\n");
        g_runtimeServerLifecycle.owner = nullptr;
        return false;
    }
    LogFormat("[NorthstarPS4] SERVER callback entries=%zu\n", g_runtimeServerCallbackList.size());
    // Before anything else: without this, a SERVER script error is invisible,
    // and the engine reports one only as "There was a problem processing game
    // logic" after tearing the level down.
    void* vm = owner ? *reinterpret_cast<void**>(reinterpret_cast<char*>(owner) + 8) : nullptr;
    void* shared = vm ? *reinterpret_cast<void**>(reinterpret_cast<char*>(vm) + 0x50) : nullptr;
    if (shared) InstallScriptPrint(shared, "SERVER", g_runtimeServerBase, g_runtimeServerSpan,
        kServerScriptPrintVa, kServerScriptPrintSinkVa);
    if (!RegisterServerConstants(owner))
        LogFormat("[NorthstarPS4] SERVER VM constant registration failed\n");
    // Constants first: a failed native registration would still leave the
    // constants in place, and the constants are what the earliest scripts need.
    if (!RegisterServerNatives(owner))
        LogFormat("[NorthstarPS4] SERVER VM native registration failed\n");
    // PC compiles every enabled mod InitScript from this VM-init hook. SERVER
    // formerly left them in scripts.rson, which made their types unavailable
    // for a correctly typed native-registration pass before ordinary scripts
    // compiled. Compile them here, in mod priority order, just like UI/CLIENT.
    static ModDiscovery mods;
    static char json[kModJsonBufferSize];
    std::memset(&mods, 0, sizeof(mods));
    CollectModNames(mods);
    using Compile = bool (*)(void*, const char*, const char*, int);
    auto compile = reinterpret_cast<Compile>(g_runtimeServerBase + kServerCompileFileVa);
    auto callback = reinterpret_cast<bool (*)(void*, const char*)>(
        g_runtimeServerBase + kServerInitCallbackVa);
    for (int i = 0; i < mods.count; ++i) {
        char metadata[256]; std::size_t size = 0;
        std::snprintf(metadata, sizeof(metadata), "%s/mod.json", mods.dirs[i]);
        const auto modHolder = std::make_unique<ModInfo>();
        ModInfo& mod = *modHolder;
        if (!ReadFileIntoBuffer(metadata, json, kModJsonBufferSize, size) ||
            !ParseModMetadata(json, mod)) return false;
        if (!mod.initScript[0]) continue;
        char normalized[256], path[320];
        if (!NormalizeRequestedPath(mod.initScript, normalized, sizeof(normalized))) return false;
        std::snprintf(path, sizeof(path), "scripts/vscripts/%s", normalized);
        const char* name = std::strrchr(normalized, '/');
        name = name ? name + 1 : normalized;
        if (!compile(owner, path, name, 0) || !compile(owner, path, name, 1)) {
            LogFormat("[NorthstarPS4] SERVER InitScript compilation failed: %s\n", path);
            return false;
        }
        LogFormat("[NorthstarPS4] SERVER InitScript compiled at VM creation: %s\n", path);
        if (mod.initScriptCallback[0] && !callback(owner, mod.initScriptCallback))
            LogFormat("[NorthstarPS4] SERVER InitScriptCallback not found: %s\n",
                mod.initScriptCallback);
    }
    if (!RegisterServerNatives(owner, true)) {
        LogFormat("[NorthstarPS4] SERVER deferred native registration failed\n");
        return false;
    }
    LogFormat("[NorthstarPS4] SERVER VM native initialization complete\n");
    // Retried on every SERVER VM creation until it succeeds: the persistence
    // interface may not be constructed yet the first time through.
    InstallRuntimePersistence();
    InstallServerChat();
    return result;
}

void RuntimeServerDestroy(void* owner) noexcept {
    auto& state = g_runtimeServerLifecycle;
    if (owner && owner == state.owner) {
        if (state.started) {
            auto call = reinterpret_cast<bool (*)(void*, const char*)>(g_runtimeServerBase + kServerInitCallbackVa);
            for (const auto& entry : *state.callbacks) if (!entry.destroy.empty()) {
                LogFormat("[NorthstarPS4] SERVER Destroy: %s\n", entry.destroy.c_str());
                if (!call(owner, entry.destroy.c_str()))
                    LogFormat("[NorthstarPS4] SERVER Destroy callback not found: %s\n", entry.destroy.c_str());
            }
        }
        if (void* vm = *reinterpret_cast<void**>(static_cast<char*>(owner) + 8))
            RemoveScriptPrint(*reinterpret_cast<void**>(static_cast<char*>(vm) + 0x50));
        uiapi::ResetAsyncCalls(uiapi::kCtxServer, "VM destroyed");
        uiapi::UnbindSquirrelHelpers(owner);
        uiapi::DropPendingLoads(owner);
        state.owner = nullptr;
        state.started = false;
        LogFormat("[NorthstarPS4] SERVER VM lifecycle state cleared\n");
    }
    reinterpret_cast<void (*)(void*)>(g_runtimeServerBase + kServerVmReleaseVa)(owner);
}

bool InstallRuntimeServerVm(OrbisKernelModule serverHandle) noexcept {
    if (g_runtimeServerVmHooked) return true;
    OrbisKernelModuleInfo info{};
    info.size = sizeof(info);
    if (sceKernelGetModuleInfo(serverHandle, &info) != 0 || info.segmentCount == 0) {
        LogFormat("[NorthstarPS4] SERVER VM hook: server.prx info unavailable\n");
        return false;
    }
    g_runtimeServerBase = reinterpret_cast<std::uintptr_t>(info.segmentInfo[0].address);
    g_runtimeServerSpan = info.segmentInfo[0].size;

    struct Gate { std::uintptr_t va; const char* bytes; std::size_t size; };
    const Gate gates[] = {
        {kServerMapSpawnCallVa, "\xe8\x47\xe4\xf1\xff", 5},
        {kServerInitCallbackVa, "\x55\x48\x89\xe5\x41\x57\x41\x56\x41\x55\x41\x54\x53\x48\x83\xec\x28", 17},
        {0x70cd56, "\x48\x8b\x3d\xa3\x7e\x6f\x00\x48\x8d\x35\x06\x31\x18\x00", 14},
        {kServerVmInitCallVa, "\xe8\x10\x2f\x00\x00", 5},
        {kServerVmInitVa, "\x55\x48\x89\xe5\x41\x57\x41\x56\x41\x55\x41\x54\x53\x48\x81\xec\xa8\x00\x00\x00", 20},
        {kServerCompileFileVa, "\x55\x48\x89\xe5\x41\x57\x41\x56\x41\x55\x41\x54\x53\x48\x83\xe4\xe0", 17},
        {kServerVmReleaseVa, "\x55\x48\x89\xe5\x41\x57\x41\x56\x53\x50\x49\x89\xff", 13},
        {0x1b60dd, "\xe8\x9e\x3d\x47\x00", 5},
        {0x1b644b, "\xe8\x30\x3a\x47\x00", 5},
        {0x70d022, "\xe8\x59\xce\xf1\xff", 5},
        {kServerInternVa, "\x55\x48\x89\xe5\x41\x57\x41\x56\x41\x55\x41\x54\x53\x48\x83\xec\x18", 17},
        {kServerInsertVa, "\x55\x48\x89\xe5\x41\x57\x41\x56\x41\x55\x41\x54\x53\x48\x83\xec", 16},
        {0x634320, "\x55\x48\x89\xe5\x41\x57\x41\x56\x53\x50\x48\x89\xfb", 13},
        {0x6346c0, "\x55\x48\x89\xe5\x41\x57\x41\x56\x53\x50", 10},
        {0x634800, "\x55\x48\x89\xe5\x41\x57\x41\x56\x53\x50", 10},
        {0x634af0, "\x55\x48\x89\xe5\x41\x57\x41\x56\x53\x50", 10},
        {0x634bf0, "\x55\x48\x89\xe5\x41\x57\x41\x56\x41\x54\x53", 11},
        {0x634ea0, "\x55\x48\x89\xe5\x41\x57\x41\x56\x41\x54\x53\x48\x83\xec\x10", 15},
        {0x636230, "\x55\x48\x89\xe5\x41\x57\x41\x56\x41\x55\x41\x54\x53\x50", 14},
        {0x6374d0, "\x55\x48\x89\xe5\x41\x57\x41\x56\x41\x55\x41\x54\x53\x48\x83\xec\x68", 17},
        {0x638ce0, "\x55\x48\x89\xe5\x41\x57\x41\x56\x53\x50\x41\x89\xf0\x49\x89\xd7\x44\x89\xc3\x81\xe3\x00\x00\x00", 24},
        {0x638db0, "\x55\x48\x89\xe5\x41\x57\x41\x56\x41\x55\x41\x54\x53\x48\x83\xec\x18", 17},
    };
    for (const auto& gate : gates) {
        if (!ValidateEnginePreimage(g_runtimeServerBase, g_runtimeServerSpan, gate.va,
                reinterpret_cast<const std::uint8_t*>(gate.bytes), gate.size)) {
            LogFormat("[NorthstarPS4] SERVER VM profile mismatch va=%lx\n", gate.va);
            g_runtimeServerBase = 0;
            return false;
        }
    }
    if (!uiapi::InstallServerDatatableRegistrarHook(g_runtimeServerBase,
            g_runtimeServerSpan, kServerRegisterSquirrelFuncVa)) {
        LogFormat("[NorthstarPS4] SERVER datatable registrar hook refused\n");
        g_runtimeServerBase = 0;
        return false;
    }

    const std::uintptr_t calls[] = {kServerVmInitCallVa, kServerMapSpawnCallVa,
                                    0x1b60dd, 0x1b644b, 0x70d022};
    const std::uintptr_t targets[] = {reinterpret_cast<std::uintptr_t>(&RuntimeServerVmInit),
                                     reinterpret_cast<std::uintptr_t>(&RuntimeServerMapSpawn),
                                     reinterpret_cast<std::uintptr_t>(&RuntimeServerDestroy),
                                     reinterpret_cast<std::uintptr_t>(&RuntimeServerDestroy),
                                     reinterpret_cast<std::uintptr_t>(&RuntimeServerDestroy)};
    constexpr int kHookCount = sizeof(calls) / sizeof(calls[0]);
    std::int32_t offsets[kHookCount]; void* pages[kHookCount];
    for (int i = 0; i < kHookCount; ++i) {
        const auto call = g_runtimeServerBase + calls[i];
        const auto relative = static_cast<std::int64_t>(targets[i]) - static_cast<std::int64_t>(call + 5);
        if (relative < -2147483648LL || relative > 2147483647LL) return false;
        offsets[i] = static_cast<std::int32_t>(relative);
        pages[i] = reinterpret_cast<void*>(call & ~std::uintptr_t(0x3fff));
    }
    // Acquire every code page before writing any hook; avoid partial install.
    for (int i = 0; i < kHookCount; ++i) if (sceKernelMprotect(pages[i], 0x4000, 7) != 0) {
        for (int j = 0; j < i; ++j) sceKernelMprotect(pages[j], 0x4000, 5);
        return false;
    }
    for (int i = 0; i < kHookCount; ++i)
        std::memcpy(reinterpret_cast<void*>(g_runtimeServerBase + calls[i] + 1), &offsets[i], 4);
    int protection = 0;
    for (auto page : pages) protection |= sceKernelMprotect(page, 0x4000, 5);
    g_runtimeServerVmHooked = true;
    InstallServerExploitFixes(g_runtimeServerBase);
    LogFormat("[NorthstarPS4] SERVER VM init, MapSpawn and destroy hooks installed base=%p protection=%d\n",
        reinterpret_cast<void*>(g_runtimeServerBase), protection);
    return protection == 0;
}

// server.prx is not loaded at boot - it never appears in the module list the
// tracker logs - and only arrives when a server actually starts, which on this
// path is `map mp_lobby`. So the hook cannot be installed once at startup like
// the client one; it has to be attempted again until the module shows up, and
// it has to win the race against the SERVER VM being created moments later.
//
// The filesystem hook is the natural place to retry from: it fires constantly
// throughout a map load, well before script compilation. Enumerating modules on
// every open would be wasteful, so attempts are throttled, and a module that is
// found but fails its gate is not retried at all.
constexpr std::int32_t kServerVmProbeInterval = 64;
std::int32_t g_serverVmProbeCounter = 0;
bool g_serverVmProbeExhausted = false;

void TryInstallServerVm() noexcept {
    if (g_runtimeServerVmHooked || g_serverVmProbeExhausted) return;
    if (++g_serverVmProbeCounter < kServerVmProbeInterval) return;
    g_serverVmProbeCounter = 0;

    OrbisKernelModule handles[kMaxModules]{};
    std::size_t available = 0;
    if (sceKernelGetModuleList(handles, sizeof(handles), &available) != 0) return;
    for (std::size_t i = 0; i < available && i < kMaxModules; ++i) {
        OrbisKernelModuleInfo info{};
        info.size = sizeof(info);
        if (sceKernelGetModuleInfo(handles[i], &info) != 0) continue;
        if (!std::strstr(info.name, "server.sprx")) continue;
        LogFormat("[NorthstarPS4] server.prx loaded; installing SERVER VM hook\n");
        // Found but unusable means the profile does not match this build, and
        // retrying cannot change that.
        if (!InstallRuntimeServerVm(handles[i])) g_serverVmProbeExhausted = true;
        return;
    }
}
