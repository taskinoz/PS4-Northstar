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
// vanilla pak they name. That is mirrored here, except that `Preload` entries
// wait for common.rpak (see LoadModRpaks).
//
// The hook is on the loader's two call sites, `+0x78c0` and `+0x7ed1`, rewritten
// with the same rel32 patcher the lifecycle hooks use. Two other approaches
// were tried and abandoned: searching module data for the function's address
// found nothing but a coincidental 64-bit match in `tier0.sprx`, and a prologue
// detour needs an executable trampoline, which shadPS4 refuses - marking this
// module's own data page RWX aborts the emulator with "Protect: Unreachable
// code!". Patching the call sites needs no new executable memory at all,
// because the loader itself is left intact and called directly as the original.
// The mechanism below is proven end to end: both call sites patch, both of
// Northstar.Custom's converted paks are dispatched after `common.rpak` with
// valid handles, and the engine opens their RPaks and redirected STARPaks. The
// first experiment failed before asset compatibility was reached:
// the engine looked for `mp_weapon_shotgun_doublebarrel.starpak` at
// `/app0/r2/`, where a mod's streamed data does not live, and then wedged.
// The code below now reads each v7 header's streamed paths and patches the
// worker's sole stream-open call at `+0x6773`, redirecting it to the owning
// mod's paks directory. That redirect is host-tested against the shipped pak;
// its live boot test was blocked by shadPS4's corrupt shader-cache assertion.
//
// This is the opposite of how the VPK work turned out, where the shipped
// archive held bytes identical to the PS4 originals. PC mod textures use
// linear blocks, while retail PS4 texture headers carry platform byte 8 and
// Morton-swizzled blocks. Discovery therefore refuses anything except the
// verified PS4 texture layout before a load request can reach the engine.
constexpr bool kModRpakLoadingEnabled = true;
constexpr std::uintptr_t kRtechLoadPakVa = 0x76f0;
constexpr std::uintptr_t kRtechLoadPakCallSites[] = {0x78c0, 0x7ed1};
constexpr std::uintptr_t kRtechOpenFileVa = 0x0a90;
constexpr std::uintptr_t kRtechStarpakOpenCallVa = 0x6773;
// The v7 loader keeps 512 entries in rtech_game's data segment.  LoadPakAsync
// writes the handle, state, copied request name and allocator at these offsets
// before publishing the entry to the worker.  The runtime module watcher can
// install this hook after the initial requests have already passed the two
// call sites, so the table also says which mod paks are overdue.
constexpr std::uintptr_t kRtechPakTableVa = 0x2a66d08;
constexpr std::size_t kRtechPakEntrySize = 0xa8;
constexpr std::size_t kRtechPakEntryCount = 512;
constexpr std::size_t kRtechPakStateOffset = 0x04;
constexpr std::size_t kRtechPakNameOffset = 0x10;
constexpr std::size_t kRtechPakAllocatorOffset = 0x20;
constexpr std::int32_t kInvalidPakHandle = -1;
// PC passes 7 for every mod pak it loads; the pak system is the same lineage
// here, so the same value is used rather than echoing the engine's own flags,
// whose meaning may be specific to the pak being loaded.
constexpr std::int32_t kModRpakFlags = 7;
constexpr const char* kPreloadAfterPak = "common.rpak";

using RtechLoadPakFn = std::int32_t (*)(const char*, void*, std::int32_t);
using RtechOpenFileFn = int (*)(const char*, void*);
RtechLoadPakFn g_originalLoadPak = nullptr;
RtechOpenFileFn g_originalRtechOpenFile = nullptr;
void* g_rpakAllocator = nullptr;
bool g_modRpakHookReady = false;
std::uintptr_t g_rtechBase = 0;

struct RuntimeModRpak {
    std::string request;  // what LoadPakAsync is given
    std::string pak;      // file name, for logs
    std::string after;    // vanilla pak it follows, empty when preload
    bool preload;
    std::int32_t handle;
};
std::vector<RuntimeModRpak> g_modRpaks;
struct RuntimeModStarpak {
    std::string embedded;
    std::string request;
};
std::vector<RuntimeModStarpak> g_modStarpaks;
std::atomic_flag g_loadingModRpaks = ATOMIC_FLAG_INIT;

bool ReadRpakStarpaks(const std::string& path, std::vector<std::string>& references) {
    references.clear();
    FILE* file = std::fopen(path.c_str(), "rb");
    if (!file) return false;
    std::uint8_t header[0x58]{};
    const bool headerRead = std::fread(header, 1, sizeof(header), file) == sizeof(header);
    if (!headerRead || std::memcmp(header, "RPak", 4) != 0 || header[4] != 7) {
        std::fclose(file);
        return false;
    }
    const std::size_t referenceSize =
        static_cast<std::size_t>(header[0x38]) |
        (static_cast<std::size_t>(header[0x39]) << 8);
    std::vector<std::uint8_t> bytes(sizeof(header) + referenceSize);
    std::memcpy(bytes.data(), header, sizeof(header));
    const bool bodyRead = referenceSize == 0 ||
        std::fread(bytes.data() + sizeof(header), 1, referenceSize, file) == referenceSize;
    std::fclose(file);
    return bodyRead && ParseRpakStarpakReferences(bytes.data(), bytes.size(), references);
}

bool ReadRpakTexturePlatforms(const std::string& path, RpakTexturePlatforms& platforms) {
    platforms = {};
    FILE* file = std::fopen(path.c_str(), "rb");
    if (!file) return false;
    if (std::fseek(file, 0, SEEK_END) != 0) {
        std::fclose(file);
        return false;
    }
    const long length = std::ftell(file);
    // Seeks to the few bytes it needs; skin packs are tens of megabytes.
    const bool inspected = length >= 0 && InspectRpakTexturePlatformsWith(
        [&](std::size_t offset, void* destination, std::size_t size) {
            return std::fseek(file, static_cast<long>(offset), SEEK_SET) == 0 &&
                std::fread(destination, 1, size, file) == size;
        },
        static_cast<std::size_t>(length), platforms);
    std::fclose(file);
    return inspected;
}

void DiscoverModRpaks() {
    g_modRpaks.clear();
    g_modStarpaks.clear();
    static ModDiscovery mods;
    CollectModNames(mods);
    for (int i = 0; i < mods.count; ++i) {
        const std::string directory = std::string(mods.dirs[i]) + "/paks";
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
            entry.request = ModRpakRequestPath(mods.dirs[i], name);
            entry.pak = name;
            entry.after = rule.after;
            entry.preload = rule.kind == RpakLoadKind::Preload;
            entry.handle = kInvalidPakHandle;
            LogFormat("[NorthstarPS4] mod rpak discovered: %s preload=%d after=%s\n",
                entry.request.c_str(), entry.preload ? 1 : 0,
                entry.after.empty() ? "(none)" : entry.after.c_str());
            g_modRpaks.push_back(std::move(entry));

            std::vector<std::string> references;
            const std::string rpakPath = directory + "/" + name;
            if (!ReadRpakStarpaks(rpakPath, references)) {
                LogFormat("[NorthstarPS4] mod rpak header refused: %s/%s\n",
                    mods.names[i], name.c_str());
                g_modRpaks.pop_back();
                continue;
            }
            RpakTexturePlatforms platforms;
            if (ReadRpakTexturePlatforms(rpakPath, platforms)) {
                LogFormat("[NorthstarPS4] mod rpak textures: %s total=%zu pc=%zu ps4=%zu other=%zu%s\n",
                    name.c_str(), platforms.textures, platforms.pc, platforms.ps4, platforms.other,
                    platforms.pc ? " conversion-required" : "");
            } else {
                LogFormat("[NorthstarPS4] mod rpak refused, unsupported archive: %s\n",
                    name.c_str());
                g_modRpaks.pop_back();
                continue;
            }
            if (!IsSupportedPs4TextureRpak(platforms)) {
                LogFormat("[NorthstarPS4] mod rpak refused, PS4 texture conversion required: %s\n",
                    name.c_str());
                g_modRpaks.pop_back();
                continue;
            }
            for (const auto& reference : references) {
                RuntimeModStarpak stream;
                stream.embedded = reference;
                stream.request = directory + "/" + reference;
                LogFormat("[NorthstarPS4] mod starpak registered: %s -> %s\n",
                    stream.embedded.c_str(), stream.request.c_str());
                g_modStarpaks.push_back(std::move(stream));
            }
        }
    }
    LogFormat("[NorthstarPS4] mod rpaks discovered=%zu\n", g_modRpaks.size());
}

int ModOpenRtechStarpak(const char* path, void* sizeOut) noexcept {
    for (const auto& stream : g_modStarpaks) {
        if (!RpakStreamPathMatches(stream.embedded, path)) continue;
        LogFormat("[NorthstarPS4] mod starpak redirect: %s -> %s\n",
            path ? path : "(null)", stream.request.c_str());
        return g_originalRtechOpenFile(stream.request.c_str(), sizeOut);
    }
    return g_originalRtechOpenFile(path, sizeOut);
}

// `requested` is the pak that just finished loading, or null for the pass
// before the engine's own load, which loads nothing on PS4 (see `due`).
void LoadModRpaks(const char* requested, void* allocator) noexcept {
    if (!g_modRpakHookReady || g_modRpaks.empty()) return;
    void* const chosen = allocator ? allocator : g_rpakAllocator;
    if (!chosen) return;
    // A mod pak's own load re-enters this hook; without the guard each one
    // would try to satisfy its own dependants.
    if (g_loadingModRpaks.test_and_set()) return;
    for (auto& pak : g_modRpaks) {
        if (pak.handle != kInvalidPakHandle) continue;
        // PS4 difference: Preload paks load once common.rpak has, not inside
        // the engine's next pak request. Loaded there, two 2048x2048 skin
        // packs left about half of all boots hung or crashed (2026-10-01).
        const bool due = requested &&
            RpakNameMatches(pak.preload ? std::string(kPreloadAfterPak) : pak.after, requested);
        if (!due) continue;
        pak.handle = g_originalLoadPak(pak.request.c_str(), chosen, kModRpakFlags);
        LogFormat("[NorthstarPS4] mod rpak load: %s after=%s handle=%d\n",
            pak.request.c_str(), requested ? requested : "(preload)", pak.handle);
    }
    g_loadingModRpaks.clear();
}

// Mod paks whose parent the engine has already loaded, from the pak table:
// the hook can be installed after the engine requested common.rpak.
void LoadOverdueModRpaks(void* allocator) noexcept {
    if (!g_rtechBase) return;
    auto* const table = reinterpret_cast<const std::uint8_t*>(g_rtechBase + kRtechPakTableVa);
    for (std::size_t i = 0; i < kRtechPakEntryCount; ++i) {
        const std::uint8_t* const entry = table + i * kRtechPakEntrySize;
        std::int32_t handle = kInvalidPakHandle;
        std::int32_t state = 0;
        const char* name = nullptr;
        std::memcpy(&handle, entry, sizeof(handle));
        std::memcpy(&state, entry + kRtechPakStateOffset, sizeof(state));
        std::memcpy(&name, entry + kRtechPakNameOffset, sizeof(name));
        // State 7 is the loader's successful terminal state (11 is failure).
        if (handle != kInvalidPakHandle && state == 7 && name) LoadModRpaks(name, allocator);
    }
}

bool ModRpaksPending() noexcept {
    for (const auto& pak : g_modRpaks)
        if (pak.handle == kInvalidPakHandle) return true;
    return false;
}

// Every mod pak is loaded here, on the thread making the engine's own request
// and after that request returns, as PC loads its Postload paks. The module
// tracker used to load overdue ones from its own thread; the pak system is not
// safe to drive from there while its own threads run, and boots crashed at
// 0x700000782c41 on either thread (2026-10-01).
std::int32_t ModLoadPakAsync(const char* path, void* allocator, std::int32_t flags) noexcept {
    // The engine hands us a working allocator on every call, so mod paks never
    // need one located independently.
    if (allocator) g_rpakAllocator = allocator;
    const std::int32_t result = g_originalLoadPak(path, allocator, flags);
    LoadModRpaks(path, allocator);
    if (ModRpaksPending()) LoadOverdueModRpaks(allocator);
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
    if (!kModRpakLoadingEnabled) return;
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
    if (!g_modStarpaks.empty()) {
        constexpr std::uint8_t starpakOpenPreimage[5] = {0xe8, 0x18, 0xa3, 0xff, 0xff};
        g_originalRtechOpenFile = reinterpret_cast<RtechOpenFileFn>(base + kRtechOpenFileVa);
        if (!PatchRpakCallSite(base, span, kRtechStarpakOpenCallVa,
                starpakOpenPreimage, reinterpret_cast<void*>(&ModOpenRtechStarpak))) {
            g_originalLoadPak = nullptr;
            g_originalRtechOpenFile = nullptr;
            LogFormat("[NorthstarPS4] mod rpak hook refused: starpak redirect unavailable\n");
            return;
        }
    }
    auto* const hook = reinterpret_cast<void*>(&ModLoadPakAsync);
    int patched = 0;
    if (PatchRpakCallSite(base, span, kRtechLoadPakCallSites[0], callPreimageA, hook)) ++patched;
    if (PatchRpakCallSite(base, span, kRtechLoadPakCallSites[1], callPreimageB, hook)) ++patched;
    if (patched == 0) {
        g_originalLoadPak = nullptr;
        LogFormat("[NorthstarPS4] mod rpak hook failed: no call sites patched\n");
        return;
    }
    g_rtechBase = base;
    g_modRpakHookReady = true;
    LogFormat("[NorthstarPS4] mod rpak hook installed sites=%d paks=%zu\n", patched, g_modRpaks.size());
}
