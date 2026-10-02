// Loose mod materials (.vmt) on PS4.
//
// materialsystem_ps4's KeyValues file loader (+0x9fb50, used for
// materials/%s.vmt) asks the filesystem's VPK cache first (ReadFromCache,
// primary slot 97). The overlay answers false for a file a mod provides, so
// that the engine opens the loose copy instead, as PC Northstar does. This
// loader only falls back to opening a .vmt from disk when its name ends in
// three numeric groups (the engine's generated materials, "___%s_%d.vmt"):
// the check is `cmp r14d, 3; jl fail` at +0x9fcdc. Any other loose material,
// such as a mod's models/weapons/defender_custom/neon.vmt, was reported
// missing and drew the checkerboard. With the jump removed, a .vmt that is not
// in a mounted VPK is opened through the filesystem (and so the mod overlay);
// a material missing everywhere still fails, after one more open attempt.
// .vtf files are read through ReadFile first and need no change here.
//
// A loaded material is then looked up in the VPK cache again (+0x4eb60, at
// +0x4f98e) to record it in its VPK entry, under a spinlock at entry+0x14.
// The result is not checked: for a material that is not in a VPK the entry
// is null and the lock wrote to address 0x14 (crash at +0x4f9b2, the first
// time a loose .vmt loaded, 2026-10-02). The 10-byte NOP at +0x4f9a6 becomes
// `test al, al; je +0x4f9fd`, past both the lock and its release.
constexpr std::uintptr_t kVmtFallbackGateVa = 0x9fcdc;
constexpr std::uintptr_t kVpkEntryLockVa = 0x4f9a6;

void InstallLooseMaterialFallback(std::uintptr_t materialSystemBase) noexcept {
    static bool done = false;
    if (!materialSystemBase || done) return;
    done = true;
    // cmp r14d, 3; jl +0xc87
    constexpr std::uint8_t gate[] = {0x41, 0x83, 0xfe, 0x03, 0x0f, 0x8c, 0x87, 0x0c, 0x00, 0x00};
    // mov cl, al; mov esi, 1; add rdx, 0x14; nop word cs:[rax+rax] (10
    // bytes); xor eax, eax; lock cmpxchg [rdx], esi
    constexpr std::uint8_t lock[] = {0x88, 0xc1, 0xbe, 0x01, 0x00, 0x00, 0x00, 0x48, 0x83, 0xc2, 0x14, 0x66, 0x2e,
        0x0f, 0x1f, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00, 0x31, 0xc0, 0xf0, 0x0f, 0xb1, 0x32};
    if (std::memcmp(reinterpret_cast<const void*>(materialSystemBase + kVmtFallbackGateVa), gate, sizeof(gate)) != 0 ||
        std::memcmp(reinterpret_cast<const void*>(materialSystemBase + kVpkEntryLockVa - 11), lock, sizeof(lock)) != 0) {
        LogFormat("[NorthstarPS4] loose material fallback refused: materialsystem profile mismatch\n");
        return;
    }
    // test al, al; je +0x4f9fd (rel32 from +0x4f9ae); xchg ax, ax
    constexpr std::uint8_t skip[] = {0x84, 0xc0, 0x0f, 0x84, 0x4f, 0x00, 0x00, 0x00, 0x66, 0x90};
    constexpr std::uint8_t nop6[] = {0x66, 0x0f, 0x1f, 0x44, 0x00, 0x00};
    if (WriteEngineCode(materialSystemBase + kVpkEntryLockVa, skip, sizeof(skip)) &&
        WriteEngineCode(materialSystemBase + kVmtFallbackGateVa + 4, nop6, sizeof(nop6)))
        LogFormat("[NorthstarPS4] loose material fallback installed\n");
    else
        LogFormat("[NorthstarPS4] loose material fallback: write failed\n");
}
