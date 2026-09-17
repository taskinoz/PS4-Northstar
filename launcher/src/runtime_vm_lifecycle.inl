bool g_runtimeVmInitHooked = false;
// PS4 CreateNewVM calls this initializer with owner in RDI, context in ESI,
// and initial time in XMM0. owner+0x3c records the context (6746fb).
// Context values match PC's ScriptContext: 0 SERVER, 1 CLIENT, 2 UI. SERVER
// lives in server.prx, which this module does not hook, so only the two
// client.prx contexts are set up here.
bool RuntimeVmInit(void* owner, int context, float time) noexcept {
    using Init = bool (*)(void*, int, float);
    const bool result = reinterpret_cast<Init>(g_runtimeClientBase + 0x6746c0)(owner, context, time);
    LogFormat("[NorthstarPS4] VM initialized context=%d owner=%p result=%d\n", context, owner, result);
    VmLifecycle* state = RuntimeLifecycleFor(context);
    if (!state) return result;
    state->owner = owner;
    state->started = false;
    const int mask = context == 2 ? uiapi::kCtxUi : uiapi::kCtxClient;
    if (!RegisterRuntimeConstants(owner, mask)) {
        LogFormat("[NorthstarPS4] %s VM native setup failed\n", state->name);
        return false;
    }
    // Same stack constraint as LoadLifecycleCallbacks: these two buffers are
    // ~25 KiB and this runs on an engine thread during VM creation.
    static ModDiscovery mods;
    static char json[kModJsonBufferSize];
    std::memset(&mods, 0, sizeof(mods));
    CollectModNames(mods);
    using Compile = bool (*)(void*, const char*, const char*, int);
    auto compile = reinterpret_cast<Compile>(g_runtimeClientBase + 0x6783f0);
    auto callback = reinterpret_cast<bool (*)(void*, const char*)>(g_runtimeClientBase + 0x679d40);
    for (int i = 0; i < mods.count; ++i) {
        char metadata[256]; std::size_t size = 0;
        std::snprintf(metadata, sizeof(metadata), "%s/%s/mod.json", kModsRoot, mods.names[i]);
        ModInfo mod{};
        if (!ReadFileIntoBuffer(metadata, json, kModJsonBufferSize, size) || !ParseModMetadata(json, mod)) return false;
        if (!mod.initScript[0]) continue;
        char normalized[256], path[320];
        if (!NormalizeRequestedPath(mod.initScript, normalized, sizeof(normalized))) return false;
        std::snprintf(path, sizeof(path), "scripts/vscripts/%s", normalized);
        const char* name = std::strrchr(normalized, '/'); name = name ? name + 1 : normalized;
        if (!compile(owner, path, name, 0) || !compile(owner, path, name, 1)) {
            LogFormat("[NorthstarPS4] %s InitScript compilation failed: %s\n", state->name, path); return false;
        }
        LogFormat("[NorthstarPS4] %s InitScript compiled at VM creation: %s\n", state->name, path);
        // PC logs and continues when an InitScriptCallback is missing rather
        // than failing the VM, so the same is done here.
        if (mod.initScriptCallback[0] && !callback(owner, mod.initScriptCallback))
            LogFormat("[NorthstarPS4] %s InitScriptCallback not found: %s\n", state->name, mod.initScriptCallback);
    }
    if (!RegisterRuntimeUiNatives(owner, mask, true)) return false;
    LogFormat("[NorthstarPS4] %s VM native initialization complete\n", state->name);
    return result;
}
bool InstallRuntimeVmInit() noexcept {
    if (g_runtimeVmInitHooked) return true;
    const struct { std::uintptr_t va; const char* bytes; std::size_t size; } gates[] = {
        {0x6717af, "\xe8\x0c\x2f\x00\x00", 5},
        {0x6746c0, "\x55\x48\x89\xe5\x41\x57\x41\x56\x41\x55\x41\x54\x53\x48\x81\xec\xa8\x00\x00\x00", 20},
        {0x6783f0, "\x55\x48\x89\xe5\x41\x57\x41\x56\x41\x55\x41\x54\x53\x48\x83\xe4\xe0", 17},
        {0x679d40, "\x55\x48\x89\xe5\x41\x57\x41\x56\x41\x55\x41\x54\x53\x48\x83\xec\x28", 17},
    };
    for (const auto& gate : gates) if (!ValidateEnginePreimage(g_runtimeClientBase, g_runtimeClientSpan, gate.va,
            reinterpret_cast<const std::uint8_t*>(gate.bytes), gate.size)) {
        LogFormat("[NorthstarPS4] VM lifecycle profile mismatch va=%lx\n", gate.va); return false;
    }
    if (!InstallRuntimeUiDestroy()) return false;
    const auto call = g_runtimeClientBase + 0x6717af;
    const auto relative = static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(&RuntimeVmInit)) - static_cast<std::int64_t>(call + 5);
    if (relative < -2147483648LL || relative > 2147483647LL) return false;
    void* page = reinterpret_cast<void*>(call & ~std::uintptr_t(0x3fff));
    if (sceKernelMprotect(page, 0x4000, 7) != 0) return false;
    const auto displacement = static_cast<std::int32_t>(relative);
    std::memcpy(reinterpret_cast<void*>(call + 1), &displacement, sizeof(displacement));
    const int protection = sceKernelMprotect(page, 0x4000, 5);
    g_runtimeVmInitHooked = true;
    LogFormat("[NorthstarPS4] VM init hook installed protection=%d\n", protection);
    return true;
}
