// Mod localisation at boot, on the game's thread (PC client/modlocalisation.cpp).
//
// PC adds every enabled mod's Localisation files right after
// CEngineVGui::Init has loaded the stock ones (r1, valve, dev). The PS4 runtime
// used to call CLocalize::AddFile from its own module-tracker thread while the
// game's main thread was still loading and using the same tables. That race
// left boots hung on the Respawn logo with a busy CPU, in about one boot in
// three (2026-10-01).
//
// The PS4 engine's equivalent of CEngineVGui::Init loads its stock files at
// engine+0x1cd4af..0x1cd518 through the CLocalize pointer at engine+0x51e9bf0
// (vtable slot 9, AddFile). The last load is conditional:
//
//   1cd4f7  cmp byte [rip+...], 0        ; engine+0x3eef284
//   1cd4fe  je 1cd518
//   1cd500  mov rdi, [rip+...]           ; CLocalize*, engine+0x51e9bf0
//   1cd507  lea rsi, [rip+...]           ; "resource/r1_%language%_lv.txt"
//   1cd50e  xor edx, edx / xor ecx, ecx
//   1cd512  mov rax, [rdi] / call [rax+0x48]
//
// Those 33 bytes become a call to EngineVguiLocaliseFiles, which makes the same
// conditional load and then adds the mods' files. Nothing jumps into the block
// and its caller-saved registers are dead at both ends (it may make a call).
//
// If the patch lands after the engine has passed that point, the host frame
// (also the main thread) adds the files on its first run instead.

namespace localiseboot {
constexpr std::uintptr_t kBlockVa = 0x1cd4f7;
constexpr std::uintptr_t kBlockEndVa = 0x1cd518;
constexpr std::uintptr_t kLvFlagVa = 0x3eef284;
constexpr std::uintptr_t kLocalizePointerVa = 0x51e9bf0;
constexpr std::uintptr_t kLvFileVa = 0x34722c;
constexpr std::uint8_t kBlock[] = {
    0x80, 0x3d, 0x86, 0x1d, 0xd2, 0x03, 0x00,  // cmp byte [rip+0x3d21d86], 0
    0x74, 0x18,                                // je +0x18
    0x48, 0x8b, 0x3d, 0xe9, 0xc6, 0x01, 0x05,  // mov rdi, [rip+0x501c6e9]
    0x48, 0x8d, 0x35, 0x1e, 0x9d, 0x17, 0x00,  // lea rsi, [rip+0x179d1e]
    0x31, 0xd2, 0x31, 0xc9,                    // xor edx, edx; xor ecx, ecx
    0x48, 0x8b, 0x07,                          // mov rax, [rdi]
    0xff, 0x50, 0x48,                          // call [rax+0x48]
};
static_assert(sizeof(kBlock) == kBlockEndVa - kBlockVa, "block size");

std::uintptr_t g_engineBase = 0;
bool g_done = false;

// The CLocalize the engine itself uses; its AddFile is checked against the
// localize.prx profile before the mods' files go through it.
bool UseEngineLocalize() noexcept {
    if (g_localiseAddFile && g_localiseThis) return true;
    if (!g_engineBase) return false;
    void* const localize = *reinterpret_cast<void* const*>(g_engineBase + kLocalizePointerVa);
    if (!localize) return false;
    auto const vtable = *reinterpret_cast<const std::uintptr_t* const*>(localize);
    const std::uintptr_t addFile = vtable ? vtable[9] : 0;
    if (!addFile || std::memcmp(reinterpret_cast<const void*>(addFile), kLocalizeAddFilePreimage,
            sizeof(kLocalizeAddFilePreimage)) != 0) {
        LogFormat("[NorthstarPS4] engine CLocalize::AddFile does not match the profile; mod localisation skipped\n");
        return false;
    }
    g_localiseAddFile = reinterpret_cast<LocaliseAddFileFn>(addFile);
    g_localiseThis = reinterpret_cast<std::uintptr_t>(localize);
    return true;
}

void AddModFiles(const char* where) noexcept {
    if (g_done) return;
    g_done = true;
    if (!UseEngineLocalize()) return;
    std::int32_t total = 0;
    std::int32_t loaded = 0;
    AddModLocalisationFiles(total, loaded);
    LogFormat("[NorthstarPS4] mod localisation added (%s): files=%d loaded=%d\n", where, total, loaded);
}
} // namespace localiseboot

// Called from the engine in place of the block above, on the game's thread.
void EngineVguiLocaliseFiles() noexcept {
    using namespace localiseboot;
    if (*reinterpret_cast<const std::uint8_t*>(g_engineBase + kLvFlagVa) != 0) {
        void* const localize = *reinterpret_cast<void* const*>(g_engineBase + kLocalizePointerVa);
        auto const vtable = *reinterpret_cast<LocaliseAddFileFn const* const*>(localize);
        vtable[9](localize, reinterpret_cast<const char*>(g_engineBase + kLvFileVa), nullptr, false);
    }
    AddModFiles("vgui init");
}

// Host frame (main thread): the files were not added at vgui init, because the
// patch landed after it or was refused.
void AddBootModLocalisationIfMissed() noexcept {
    if (!localiseboot::g_done && (localiseboot::g_engineBase || g_localiseAddFile))
        localiseboot::AddModFiles("first host frame");
}

// Whether the engine's CLocalize is the localize.prx singleton the tracker profiled.
bool BootLocalizeIs(std::uintptr_t localize) noexcept {
    return localiseboot::g_engineBase &&
        *reinterpret_cast<const std::uintptr_t*>(localiseboot::g_engineBase + localiseboot::kLocalizePointerVa) == localize;
}

// As soon as the engine module is mapped, before it reaches vgui init.
void InstallBootLocalisation(std::uintptr_t engineBase, std::size_t engineSize) noexcept {
    using namespace localiseboot;
    if (g_engineBase) return;
    // The block's rip-relative operands pin the flag, pointer and string too.
    if (!ValidateEnginePreimage(engineBase, engineSize, kBlockVa, kBlock, sizeof(kBlock))) {
        LogFormat("[NorthstarPS4] boot localisation hook refused: engine profile mismatch\n");
        return;
    }
    const auto call = engineBase + kBlockVa;
    const auto relative = static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(&EngineVguiLocaliseFiles)) -
        static_cast<std::int64_t>(call + 5);
    if (relative < -2147483648LL || relative > 2147483647LL) {
        LogFormat("[NorthstarPS4] boot localisation hook refused: branch outside rel32 range\n");
        return;
    }
    g_engineBase = engineBase;
    std::uint8_t patch[sizeof(kBlock)];
    std::memset(patch, 0x90, sizeof(patch));
    patch[0] = 0xe8;
    const auto displacement = static_cast<std::int32_t>(relative);
    std::memcpy(patch + 1, &displacement, sizeof(displacement));
    if (!WriteEngineCode(call, patch, sizeof(patch))) {
        g_engineBase = 0;
        LogFormat("[NorthstarPS4] boot localisation hook: write failed\n");
        return;
    }
    LogFormat("[NorthstarPS4] boot localisation hook installed (engine vgui init)\n");
}
