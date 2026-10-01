// Native deferred-call drain. PC Northstar runs each Squirrel manager's
// message buffer after CHostState::FrameUpdate. In this PS4 build Clang
// inlined the state handlers into engine.prx+0x1334d0; its sole frame-loop
// caller is the call at +0x176151.
//
// The profile is deliberately tied to CUSA04013's 2017-12-05 engine.prx.
// Refuse a different build instead of patching a guessed call site.
constexpr std::uintptr_t kHostFrameUpdateVa = 0x1334d0;
constexpr std::uintptr_t kHostFrameUpdateCallVa = 0x176151;
constexpr std::uint8_t kHostFrameUpdatePreimage[] = {
    0xe8, 0x7a, 0xd3, 0xfb, 0xff,
};
constexpr std::uint8_t kHostFrameUpdateFunctionPreimage[] = {
    0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56,
    0x41, 0x55, 0x41, 0x54, 0x53, 0x48, 0x81, 0xec,
    0xd8, 0x01, 0x00, 0x00,
};

using HostFrameUpdateFn = void (*)(double, float);
HostFrameUpdateFn g_originalHostFrameUpdate = nullptr;
bool g_hostFrameHookInstalled = false;
bool g_hostFrameContextSeen[3]{};

void RuntimeHostFrameUpdate(double currentTime, float frameTime) noexcept {
    g_originalHostFrameUpdate(currentTime, frameTime);
#if defined(NORTHSTAR_PS4_ENABLE_RUNTIME_MANIFEST) && defined(NORTHSTAR_PS4_ENABLE_M6_LOCALISE) && \
    defined(NORTHSTAR_PS4_ENABLE_M6_MOD_METADATA)
    AddBootModLocalisationIfMissed();
#endif

    // ScriptContext values are SERVER=0, CLIENT=1 and UI=2. The async API
    // uses Northstar's context masks instead, hence the explicit mapping.
    constexpr int masks[] = {
        uiapi::kCtxServer, uiapi::kCtxClient, uiapi::kCtxUi,
    };
    constexpr const char* names[] = {"SERVER", "CLIENT", "UI"};
    for (int context = 0; context < 3; ++context) {
        void* const vm = uiapi::g_squirrelHelperBindings[context].vm;
        if (!vm) continue;
        if (!g_hostFrameContextSeen[context]) {
            g_hostFrameContextSeen[context] = true;
            LogFormat("[NorthstarPS4] native async drain active context=%s vm=%p\n",
                names[context], vm);
        }
        uiapi::DrainPendingLoadsForVm(vm);
        uiapi::RunAsyncCalls(vm, masks[context]);
    }
}

bool InstallRuntimeHostFrame(std::uintptr_t engineBase, std::size_t engineSpan) noexcept {
    if (g_hostFrameHookInstalled) return true;
    if (!ValidateEnginePreimage(engineBase, engineSpan, kHostFrameUpdateVa,
            kHostFrameUpdateFunctionPreimage,
            sizeof(kHostFrameUpdateFunctionPreimage)) ||
        !ValidateEnginePreimage(engineBase, engineSpan, kHostFrameUpdateCallVa,
            kHostFrameUpdatePreimage, sizeof(kHostFrameUpdatePreimage))) {
        LogFormat("[NorthstarPS4] native async frame hook refused: profile mismatch\n");
        return false;
    }

    const auto call = engineBase + kHostFrameUpdateCallVa;
    const auto relative = static_cast<std::int64_t>(
        reinterpret_cast<std::uintptr_t>(&RuntimeHostFrameUpdate)) -
        static_cast<std::int64_t>(call + 5);
    if (relative < -2147483648LL || relative > 2147483647LL) {
        LogFormat("[NorthstarPS4] native async frame hook refused: branch outside rel32 range\n");
        return false;
    }
    void* const page = reinterpret_cast<void*>(call & ~std::uintptr_t(0x3fff));
    if (sceKernelMprotect(page, 0x4000, 7) != 0) {
        LogFormat("[NorthstarPS4] native async frame hook: mprotect failed\n");
        return false;
    }
    // Publish the original before the call site can reach the wrapper. The
    // module tracker installs this from a detached thread while frames run.
    g_originalHostFrameUpdate = reinterpret_cast<HostFrameUpdateFn>(
        engineBase + kHostFrameUpdateVa);
    const auto displacement = static_cast<std::int32_t>(relative);
    std::memcpy(reinterpret_cast<void*>(call + 1), &displacement,
        sizeof(displacement));
    const int protection = sceKernelMprotect(page, 0x4000, 5);
    if (protection != 0) {
        LogFormat("[NorthstarPS4] native async frame hook: restore protection failed=%d\n",
            protection);
        return false;
    }
    g_hostFrameHookInstalled = true;
    LogFormat("[NorthstarPS4] native async frame hook installed call=engine+%lx\n",
        kHostFrameUpdateCallVa);
    return true;
}
