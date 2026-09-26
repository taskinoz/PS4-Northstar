// Mod script-callback lifecycle for each script context.
//
// The retail call sites are replaced, not the shared callback function, so
// every call below reaches the untouched engine implementation. PC hooks
// `CallScriptInitCallback` once and filters by callback name; the PS4 build
// patches the specific call sites that name matches instead:
//   UI      UICodeCallback_UIInit      VA 0x31df24
//   CLIENT  ClientCodeCallback_MapSpawn VA 0x768386
// which is the same set PC's `bShouldCallCustomCallbacks` allows through.
//
// This module's DT_INIT is routed straight at the module initializer, so the
// usual crt walk of .init_array never happens and no C++ dynamic initializer
// runs. Every global here must therefore be constant-initialized: the callback
// vectors are separate zero-initialized globals rather than members, because a
// std::vector member would make the whole aggregate initializer dynamic and
// leave these string pointers null at runtime.
struct VmLifecycle {
    const char* name;          // log label
    const char* metadataKey;   // mod.json Scripts[] key
    void* owner;               // active CSquirrelVM, null when none
    bool started;              // a lifecycle callback has run for this owner
    std::vector<ScriptCallback>* callbacks;
};
std::vector<ScriptCallback> g_runtimeUiCallbackList;
std::vector<ScriptCallback> g_runtimeClientCallbackList;
VmLifecycle g_runtimeUiLifecycle{"UI", "UICallback", nullptr, false, &g_runtimeUiCallbackList};
VmLifecycle g_runtimeClientLifecycle{"CLIENT", "ClientCallback", nullptr, false, &g_runtimeClientCallbackList};
VmLifecycle* const g_runtimeLifecycles[] = {&g_runtimeUiLifecycle, &g_runtimeClientLifecycle};
bool g_runtimeCallbackHooked = false;

VmLifecycle* RuntimeLifecycleFor(int context) noexcept {
    if (context == 2) return &g_runtimeUiLifecycle;
    if (context == 1) return &g_runtimeClientLifecycle;
    return nullptr;   // SERVER: g_runtimeServerLifecycle in runtime_server_vm.inl
}

// PC ignores each mod callback's result and always runs the rest; only the
// engine's own callback decides the return value. A mod whose callback is
// declared but never defined must not stop the mods that load after it.
// Defined in runtime_console.inl, which is included later in the same unit.
void RunConsoleSelfTest() noexcept;
void RunHttpProbe() noexcept;
void DrainConsoleCommandFile() noexcept;

bool DispatchLifecycle(VmLifecycle& state, void* owner, const char* callback) noexcept {
    auto original = reinterpret_cast<bool (*)(void*, const char*)>(g_runtimeClientBase + 0x679d40);
    state.started = true;
    for (const auto& entry : *state.callbacks) if (!entry.before.empty()) {
        LogFormat("[NorthstarPS4] %s Before: %s\n", state.name, entry.before.c_str());
        if (!original(owner, entry.before.c_str()))
            LogFormat("[NorthstarPS4] %s Before callback not found: %s\n", state.name, entry.before.c_str());
    }
    const bool result = original(owner, callback);
    for (const auto& entry : *state.callbacks) if (!entry.after.empty()) {
        LogFormat("[NorthstarPS4] %s After: %s\n", state.name, entry.after.c_str());
        if (!original(owner, entry.after.c_str()))
            LogFormat("[NorthstarPS4] %s After callback not found: %s\n", state.name, entry.after.c_str());
    }
    uiapi::DrainPendingLoads(owner);
    LogFormat("[NorthstarPS4] %s lifecycle completed\n", state.name);
    // Late enough that the engine's command system is up, and it runs again on
    // every map and menu change, which is what gives queued commands a tick.
    RunConsoleSelfTest();
    RunHttpProbe();
    DrainConsoleCommandFile();
    return result;
}

bool RuntimeUiInit(void* owner, const char* callback) noexcept {
    auto original = reinterpret_cast<bool (*)(void*, const char*)>(g_runtimeClientBase + 0x679d40);
    // PC drains queued script calls from CHostState::FrameUpdate; the code
    // callback path is the closest PS4 equivalent profiled so far.
    uiapi::DrainPendingLoads(owner);
    if (!callback || std::strcmp(callback, "UICodeCallback_UIInit")) return original(owner, callback);
    return DispatchLifecycle(g_runtimeUiLifecycle, owner, callback);
}

bool RuntimeClientMapSpawn(void* owner, const char* callback) noexcept {
    auto original = reinterpret_cast<bool (*)(void*, const char*)>(g_runtimeClientBase + 0x679d40);
    uiapi::DrainPendingLoads(owner);
    if (!callback || std::strcmp(callback, "ClientCodeCallback_MapSpawn")) return original(owner, callback);
    return DispatchLifecycle(g_runtimeClientLifecycle, owner, callback);
}

// The discovery list and the metadata buffer are ~25 KiB together and this
// runs during VM init on an engine thread with a small stack, so they are kept
// out of the frame; the two calls below are sequential and single-threaded.
ModDiscovery g_lifecycleDiscovery;
char g_lifecycleJson[kModJsonBufferSize];
// noinline keeps the two calls below from being merged into one frame; the
// engine thread that runs VM init has far less stack than the main thread.
__attribute__((noinline)) bool LoadLifecycleCallbacks(VmLifecycle& state) noexcept {
    ModDiscovery& mods = g_lifecycleDiscovery;
    std::memset(&mods, 0, sizeof(mods));
    CollectModNames(mods);
    char* json = g_lifecycleJson;
    std::vector<ScriptCallback> callbacks;
    for (int i = 0; i < mods.count; ++i) {
        char path[256];
        std::size_t size = 0;
        std::snprintf(path, sizeof(path), "%s/%s/mod.json", kModsRoot, mods.names[i]);
        std::vector<ScriptCallback> entries;
        if (!ReadFileIntoBuffer(path, json, kModJsonBufferSize, size) ||
            !ParseScriptCallbacks(json, state.metadataKey, entries)) {
            LogFormat("[NorthstarPS4] %s callback metadata rejected: %s\n", state.name, path);
            return false;
        }
        callbacks.insert(callbacks.end(), entries.begin(), entries.end());
    }
    state.callbacks->swap(callbacks);
    return true;
}

// Rewrites one call site's rel32 to `target`, refusing anything out of range.
bool PatchCallSite(std::uintptr_t va, const std::uint8_t (&preimage)[5], void* target, const char* label) noexcept {
    if (!ValidateEnginePreimage(g_runtimeClientBase, g_runtimeClientSpan, va, preimage, 5)) {
        LogFormat("[NorthstarPS4] %s hook refused: preimage mismatch va=%lx\n", label, va);
        return false;
    }
    const auto call = g_runtimeClientBase + va;
    const auto relative = static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(target)) - static_cast<std::int64_t>(call + 5);
    if (relative < -2147483648LL || relative > 2147483647LL) {
        LogFormat("[NorthstarPS4] %s hook refused: branch outside rel32 range\n", label);
        return false;
    }
    void* page = reinterpret_cast<void*>(call & ~std::uintptr_t(0x3fff));
    if (sceKernelMprotect(page, 0x4000, 7) != 0) {
        LogFormat("[NorthstarPS4] %s hook: mprotect failed\n", label);
        return false;
    }
    const auto displacement = static_cast<std::int32_t>(relative);
    std::memcpy(reinterpret_cast<void*>(call + 1), &displacement, sizeof(displacement));
    const int protection = sceKernelMprotect(page, 0x4000, 5);
    LogFormat("[NorthstarPS4] %s hook installed va=%lx protection=%d\n", label, va, protection);
    return protection == 0;
}

bool InstallRuntimeUiCallbacks() noexcept {
    if (g_runtimeCallbackHooked) return true;
    constexpr std::uint8_t functionBytes[] = {0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x83,0xec,0x28};
    if (!ValidateEnginePreimage(g_runtimeClientBase, g_runtimeClientSpan, 0x679d40, functionBytes, sizeof(functionBytes))) return false;
    // Parse every mod's metadata before mutating anything.
    if (!LoadLifecycleCallbacks(g_runtimeUiLifecycle) || !LoadLifecycleCallbacks(g_runtimeClientLifecycle)) return false;
    constexpr std::uint8_t uiCall[5] = {0xe8,0x17,0xbe,0x35,0x00};
    constexpr std::uint8_t clientCall[5] = {0xe8,0xb5,0x19,0xf1,0xff};
    if (!PatchCallSite(0x31df24, uiCall, reinterpret_cast<void*>(&RuntimeUiInit), "UI lifecycle")) return false;
    // A CLIENT VM only exists once a map loads. Failing to hook it must not
    // take the working UI path down with it.
    if (!PatchCallSite(0x768386, clientCall, reinterpret_cast<void*>(&RuntimeClientMapSpawn), "CLIENT lifecycle"))
        LogFormat("[NorthstarPS4] CLIENT lifecycle callbacks unavailable; UI remains active\n");
    g_runtimeCallbackHooked = true;
    LogFormat("[NorthstarPS4] lifecycle callbacks loaded ui=%zu client=%zu\n",
        g_runtimeUiLifecycle.callbacks->size(), g_runtimeClientLifecycle.callbacks->size());
    return true;
}

// Four retail teardown paths release owner->sqvm through 6787a0 and then free
// the wrapper through 67def0: three for the UI VM (which clear the owner
// global at 0x1afbfb8) and one for the CLIENT VM (owner global 0x19d4fe8,
// disassembled at 0x2e77f2-0x2e7834). Dispatch before the first release.
void RuntimeUiDestroy(void* owner) noexcept {
    for (VmLifecycle* state : g_runtimeLifecycles) {
        if (owner != state->owner) continue;
        if (state->started) {
            auto call = reinterpret_cast<bool (*)(void*, const char*)>(g_runtimeClientBase + 0x679d40);
            for (const auto& entry : *state->callbacks) if (!entry.destroy.empty()) {
                LogFormat("[NorthstarPS4] %s Destroy: %s\n", state->name, entry.destroy.c_str());
                if (!call(owner, entry.destroy.c_str()))
                    LogFormat("[NorthstarPS4] %s Destroy callback not found: %s\n", state->name, entry.destroy.c_str());
            }
        }
        // Clear before release, including partial or failed startup and
        // addresses the allocator may hand back for a later VM.
        if (void* vm = *reinterpret_cast<void**>(static_cast<char*>(owner) + 8))
            RemoveScriptPrint(*reinterpret_cast<void**>(static_cast<char*>(vm) + 0x50));
        uiapi::DropPendingLoads(owner);
        state->owner = nullptr;
        state->started = false;
        LogFormat("[NorthstarPS4] %s VM lifecycle state cleared\n", state->name);
    }
    reinterpret_cast<void (*)(void*)>(g_runtimeClientBase + 0x6787a0)(owner);
}

bool InstallRuntimeUiDestroy() noexcept {
    static bool installed = false;
    if (installed) return true;
    constexpr std::uint8_t releaseBytes[] = {0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x53,0x50,0x49,0x89,0xff};
    if (!ValidateEnginePreimage(g_runtimeClientBase, g_runtimeClientSpan, 0x6787a0, releaseBytes, sizeof(releaseBytes))) return false;
    constexpr int kSites = 4;
    const struct { std::uintptr_t va; std::uint8_t bytes[5]; } sites[kSites] = {
        {0x1441ce, {0xe8,0xcd,0x45,0x53,0x00}},   // UI
        {0x31e02a, {0xe8,0x71,0xa7,0x35,0x00}},   // UI
        {0x33559a, {0xe8,0x01,0x32,0x34,0x00}},   // UI
        {0x2e7813, {0xe8,0x88,0x0f,0x39,0x00}},   // CLIENT
    };
    std::int32_t offsets[kSites];
    void* pages[kSites];
    for (int i = 0; i < kSites; ++i) {
        if (!ValidateEnginePreimage(g_runtimeClientBase, g_runtimeClientSpan, sites[i].va, sites[i].bytes, 5)) {
            LogFormat("[NorthstarPS4] destroy hook preimage mismatch va=%lx\n", sites[i].va);
            return false;
        }
        const auto address = g_runtimeClientBase + sites[i].va;
        const auto distance = static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(&RuntimeUiDestroy)) - static_cast<std::int64_t>(address + 5);
        if (distance < -2147483648LL || distance > 2147483647LL) return false;
        offsets[i] = static_cast<std::int32_t>(distance);
        pages[i] = reinterpret_cast<void*>(address & ~std::uintptr_t(0x3fff));
    }
    // Acquire every page before changing any instruction.
    for (int i = 0; i < kSites; ++i) if (sceKernelMprotect(pages[i], 0x4000, 7) != 0) {
        for (int j = 0; j < i; ++j) sceKernelMprotect(pages[j], 0x4000, 5);
        return false;
    }
    for (int i = 0; i < kSites; ++i)
        std::memcpy(reinterpret_cast<void*>(g_runtimeClientBase + sites[i].va + 1), &offsets[i], 4);
    int protection = 0;
    for (int i = 0; i < kSites; ++i) protection |= sceKernelMprotect(pages[i], 0x4000, 5);
    installed = true;
    LogFormat("[NorthstarPS4] VM destroy hooks installed count=%d protection=%d\n", kSites, protection);
    return protection == 0;
}
