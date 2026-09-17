// Northstar mod rpaks.
//
// `rtech_game.prx` owns the pak system. `+0x76f0` is the loader: it takes the
// lock at `+0x2a7460c`, allocates a handle (-1 on failure), indexes a 512-slot
// table of 0xa8-byte entries by `handle & 0x1ff`, stores flags at `+0x380638`
// and state 1 at `+0x380634`, strlens the name, calls
// `allocator->alloc(allocator, len + 1, 1)` through the allocator's first
// vtable slot, copies the name in and returns the handle. The worker at
// `+0x5340` later turns that name into a path with `/app0/r2/paks/PS4/%s`.
// `+0x78b0` wraps it for the rest of the game, forwarding rdi/rsi/edx and
// handing rcx and r8 to the completion registrar at `+0x74b0`.
//
// PC hooks the same call and loads each enabled mod's paks around it: `Preload`
// entries before the engine's own load, `Postload` entries straight after the
// vanilla pak they name. That is mirrored here.
//
// The hook is on the loader's two call sites, `+0x78c0` and `+0x7ed1`, rewritten
// with the same rel32 patcher the lifecycle hooks use. Two other approaches
// were tried and abandoned: searching module data for the function's address
// found nothing but a coincidental 64-bit match in `tier0.sprx`, and a prologue
// detour needs an executable trampoline, which shadPS4 refuses - marking this
// module's own data page RWX aborts the emulator with "Protect: Unreachable
// code!". Patching the call sites needs no new executable memory at all,
// because the loader itself is left intact and called directly as the original.
// Off by default, and deliberately so. The mechanism below is proven end to
// end: both call sites patch, both of Northstar.Custom's paks are dispatched
// after `common.rpak` with valid handles, and the engine opens and reads
// `mp_weapon_shotgun_doublebarrel.rpak` from the mod directory with no pak
// error. What fails is the payload. Those archives are PC builds - `RPak`
// version 7 but flags 0x0000 against retail's 0x0100 - carrying `txtr` and
// `matl` assets in PC formats, and loading them wedges the boot: the engine
// looks for `mp_weapon_shotgun_doublebarrel.starpak` at `/app0/r2/`, where a
// mod's streamed data does not live, and the pak system then walks the address
// space in `sceKernelAvailableDirectMemorySize` in 0x4000 steps and never
// finishes. No playlist, no UI lifecycle, `UI VM probe timed out`.
//
// This is the opposite of how the VPK work turned out, where the shipped
// archive held bytes identical to the PS4 originals. Turn this on once a mod
// ships PS4-format paks, and resolve starpaks then too - PC registers them
// separately rather than leaving them beside the rpak.
constexpr bool kModRpakLoadingEnabled = false;
constexpr std::uintptr_t kRtechLoadPakVa = 0x76f0;
constexpr std::uintptr_t kRtechLoadPakCallSites[] = {0x78c0, 0x7ed1};
constexpr std::int32_t kInvalidPakHandle = -1;
// PC passes 7 for every mod pak it loads; the pak system is the same lineage
// here, so the same value is used rather than echoing the engine's own flags,
// whose meaning may be specific to the pak being loaded.
constexpr std::int32_t kModRpakFlags = 7;

using RtechLoadPakFn = std::int32_t (*)(const char*, void*, std::int32_t);
RtechLoadPakFn g_originalLoadPak = nullptr;
void* g_rpakAllocator = nullptr;
bool g_modRpakHookReady = false;

struct RuntimeModRpak {
    std::string request;  // what LoadPakAsync is given
    std::string pak;      // file name, for logs
    std::string after;    // vanilla pak it follows, empty when preload
    bool preload;
    std::int32_t handle;
};
std::vector<RuntimeModRpak> g_modRpaks;
std::atomic_flag g_loadingModRpaks = ATOMIC_FLAG_INIT;

void DiscoverModRpaks() {
    g_modRpaks.clear();
    ModDiscovery mods{};
    CollectModNames(mods);
    for (int i = 0; i < mods.count; ++i) {
        const std::string directory = std::string(kModsRoot) + "/" + mods.names[i] + "/paks";
        DIR* dir = opendir(directory.c_str());
        if (!dir) continue;
        static char config[kModJsonBufferSize];
        std::size_t size = 0;
        const bool readConfig =
            ReadFileIntoBuffer((directory + "/rpak.json").c_str(), config, sizeof(config), size);
        std::vector<std::string> names;
        while (dirent* entry = readdir(dir)) {
            if (IsRpakFileName(entry->d_name)) names.push_back(entry->d_name);
        }
        closedir(dir);
        std::sort(names.begin(), names.end());
        for (const auto& name : names) {
            bool configUsable = false;
            const RpakRule rule = ResolveRpakRule(name, readConfig ? config : nullptr, configUsable);
            if (!configUsable) {
                // PC warns and leaves the pak alone rather than guessing when
                // it should load. Loading at the wrong time is worse than not
                // loading at all.
                LogFormat("[NorthstarPS4] mod rpak skipped, no usable rpak.json: %s/%s\n",
                    mods.names[i], name.c_str());
                continue;
            }
            if (rule.kind == RpakLoadKind::None) {
                LogFormat("[NorthstarPS4] mod rpak skipped, no load rule: %s/%s\n",
                    mods.names[i], name.c_str());
                continue;
            }
            RuntimeModRpak entry;
            entry.request = ModRpakRequestPath(mods.names[i], name);
            entry.pak = name;
            entry.after = rule.after;
            entry.preload = rule.kind == RpakLoadKind::Preload;
            entry.handle = kInvalidPakHandle;
            LogFormat("[NorthstarPS4] mod rpak discovered: %s preload=%d after=%s\n",
                entry.request.c_str(), entry.preload ? 1 : 0,
                entry.after.empty() ? "(none)" : entry.after.c_str());
            g_modRpaks.push_back(std::move(entry));
        }
    }
    LogFormat("[NorthstarPS4] mod rpaks discovered=%zu\n", g_modRpaks.size());
}

// `requested` is null for the preload pass, which runs before the engine's own
// load; otherwise it is the pak that just finished loading.
void LoadModRpaks(const char* requested, void* allocator) noexcept {
    if (!g_modRpakHookReady || g_modRpaks.empty()) return;
    void* const chosen = allocator ? allocator : g_rpakAllocator;
    if (!chosen) return;
    // A mod pak's own load re-enters this hook; without the guard each one
    // would try to satisfy its own dependants.
    if (g_loadingModRpaks.test_and_set()) return;
    for (auto& pak : g_modRpaks) {
        if (pak.handle != kInvalidPakHandle) continue;
        const bool due = requested ? (!pak.preload && RpakNameMatches(pak.after, requested))
                                   : pak.preload;
        if (!due) continue;
        pak.handle = g_originalLoadPak(pak.request.c_str(), chosen, kModRpakFlags);
        LogFormat("[NorthstarPS4] mod rpak load: %s after=%s handle=%d\n",
            pak.request.c_str(), requested ? requested : "(preload)", pak.handle);
    }
    g_loadingModRpaks.clear();
}

std::int32_t ModLoadPakAsync(const char* path, void* allocator, std::int32_t flags) noexcept {
    // The engine hands us a working allocator on every call, so mod paks never
    // need one located independently.
    if (allocator) g_rpakAllocator = allocator;
    LoadModRpaks(nullptr, allocator);
    const std::int32_t result = g_originalLoadPak(path, allocator, flags);
    LoadModRpaks(path, allocator);
    return result;
}

// Rewrites one `call rel32` to reach `target` instead. Same shape as the
// lifecycle hooks' patcher, but against rtech_game rather than the client.
bool PatchRpakCallSite(std::uintptr_t base, std::size_t span, std::uintptr_t va,
    const std::uint8_t (&preimage)[5], void* target) noexcept {
    if (!ValidateEnginePreimage(base, span, va, preimage, sizeof(preimage))) {
        LogFormat("[NorthstarPS4] mod rpak call site refused: preimage mismatch va=%lx\n", va);
        return false;
    }
    const std::uintptr_t call = base + va;
    const auto relative = static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(target)) -
        static_cast<std::int64_t>(call + 5);
    if (relative < -2147483648LL || relative > 2147483647LL) {
        LogFormat("[NorthstarPS4] mod rpak call site refused: branch outside rel32 range va=%lx\n", va);
        return false;
    }
    void* const page = reinterpret_cast<void*>(call & ~std::uintptr_t(0x3fff));
    if (sceKernelMprotect(page, 0x4000, 7) != 0) {
        LogFormat("[NorthstarPS4] mod rpak call site: mprotect failed va=%lx\n", va);
        return false;
    }
    const auto displacement = static_cast<std::int32_t>(relative);
    std::memcpy(reinterpret_cast<void*>(call + 1), &displacement, sizeof(displacement));
    const int protection = sceKernelMprotect(page, 0x4000, 5);
    LogFormat("[NorthstarPS4] mod rpak call site patched va=%lx protection=%d\n", va, protection);
    return protection == 0;
}

void InstallModRpakHook(OrbisKernelModule rtechHandle) noexcept {
    if (!kModRpakLoadingEnabled) {
        LogFormat("[NorthstarPS4] mod rpak loading disabled: shipped mod paks are PC builds\n");
        return;
    }
    OrbisKernelModuleInfo info{};
    info.size = sizeof(info);
    if (sceKernelGetModuleInfo(rtechHandle, &info) != 0 || info.segmentCount == 0) {
        LogFormat("[NorthstarPS4] mod rpak hook: rtech_game info unavailable\n");
        return;
    }
    const auto base = reinterpret_cast<std::uintptr_t>(info.segmentInfo[0].address);
    const std::size_t span = info.segmentInfo[0].size;
    // The loader's prologue, so a different build is refused before anything
    // is written.
    constexpr std::uint8_t loaderPreimage[] = {
        0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56, 0x41, 0x55,
        0x41, 0x54, 0x53, 0x48, 0x83, 0xec, 0x18, 0x48, 0x89, 0x7d};
    if (!ValidateEnginePreimage(base, span, kRtechLoadPakVa, loaderPreimage, sizeof(loaderPreimage))) {
        LogFormat("[NorthstarPS4] mod rpak hook refused: loader preimage mismatch\n");
        return;
    }
    DiscoverModRpaks();
    if (g_modRpaks.empty()) {
        LogFormat("[NorthstarPS4] mod rpak hook skipped: no mod rpaks to load\n");
        return;
    }
    // Both sites call the same loader, so both carry a rel32 back to it.
    constexpr std::uint8_t callPreimageA[5] = {0xe8, 0x2b, 0xfe, 0xff, 0xff};
    constexpr std::uint8_t callPreimageB[5] = {0xe8, 0x1a, 0xf8, 0xff, 0xff};
    g_originalLoadPak = reinterpret_cast<RtechLoadPakFn>(base + kRtechLoadPakVa);
    auto* const hook = reinterpret_cast<void*>(&ModLoadPakAsync);
    int patched = 0;
    if (PatchRpakCallSite(base, span, kRtechLoadPakCallSites[0], callPreimageA, hook)) ++patched;
    if (PatchRpakCallSite(base, span, kRtechLoadPakCallSites[1], callPreimageB, hook)) ++patched;
    if (patched == 0) {
        g_originalLoadPak = nullptr;
        LogFormat("[NorthstarPS4] mod rpak hook failed: no call sites patched\n");
        return;
    }
    g_modRpakHookReady = true;
    LogFormat("[NorthstarPS4] mod rpak hook installed sites=%d paks=%zu\n", patched, g_modRpaks.size());
}
