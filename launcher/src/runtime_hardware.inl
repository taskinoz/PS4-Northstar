// What a PS4 does differently from shadPS4, for the rest of the runtime.
// Loaded as a GoldHEN plugin on a PS4 (2026-10-07), the first boots found:
// - a thread's default stack is too small for mod discovery's 47 KiB frames;
// - this PRX is mapped near 0x800000000, gigabytes from the game's modules,
//   so a 5-byte call or jump from game code cannot reach it;
// - modules report `.prx` names where shadPS4 reports `.sprx`.

// Attributes for every thread this runtime starts: 256 KiB, over twice the
// deepest stack mod discovery needs. Stacks come out of the game's flexible
// memory, so not more.
const OrbisPthreadAttr* RuntimeThreadAttr() noexcept {
    static OrbisPthreadAttr attr{};
    static bool ready = false;
    if (!ready) {
        scePthreadAttrInit(&attr);
        scePthreadAttrSetstacksize(&attr, 256 * 1024);
        ready = true;
    }
    return &attr;
}

// The length of a protection change, from the page holding `address`, that
// covers `size` bytes. A patched displacement can straddle two 16 KiB pages,
// and a PS4 enforces protection per page: writing the second page's bytes
// faulted in client.prx (2026-10-07). shadPS4 never caught it.
std::size_t PageSpan(std::uintptr_t address, std::size_t size) noexcept {
    const auto first = address & ~std::uintptr_t(0x3fff);
    const auto last = (address + size - 1) & ~std::uintptr_t(0x3fff);
    return last - first + 0x4000;
}

// Whether `size` bytes at `address` can be read, page by page. On a PS4, a
// read of client.prx's code faulted as "page not present" late in the boot
// (2026-10-07), although the same code was read earlier; an unusual page is
// logged with what the kernel reports, and refused.
bool CodeReadable(std::uintptr_t address, std::size_t size) noexcept {
    const auto last = (address + size - 1) & ~std::uintptr_t(0x3fff);
    for (auto page = address & ~std::uintptr_t(0x3fff); page <= last; page += 0x4000) {
        void* start = nullptr;
        void* end = nullptr;
        int prot = 0;
        const int query = sceKernelQueryMemoryProtection(reinterpret_cast<void*>(page), &start, &end, &prot);
        OrbisKernelVirtualQueryInfo info{};
        const int virtualQuery = sceKernelVirtualQuery(reinterpret_cast<const void*>(page), 0, &info, sizeof(info));
        if (query == 0 && (prot & 1) && virtualQuery == 0 && info.isCommitted) continue;
        LogFormat("[NorthstarPS4] unreadable code page %p: protection query=0x%x %p-%p prot=0x%x; "
            "virtual query=0x%x committed=%u flexible=%u direct=%u name=%.32s\n",
            reinterpret_cast<void*>(page), query, start, end, prot, virtualQuery, info.isCommitted,
            info.isFlexibleMemory, info.isDirectMemory, info.name);
        if (query != 0 || !(prot & 1)) {
            // Is it this page alone, or the whole region? And what is mapped next?
            for (const std::intptr_t delta : {-0x10000L, -0x4000L, 0x4000L, 0x10000L}) {
                const auto near = page + delta;
                const int nearQuery = sceKernelQueryMemoryProtection(reinterpret_cast<void*>(near), &start, &end, &prot);
                LogFormat("[NorthstarPS4]   page %+ld: query=0x%x %p-%p prot=0x%x\n", static_cast<long>(delta),
                    nearQuery, start, end, prot);
            }
            OrbisKernelVirtualQueryInfo next{};
            const int nextQuery = sceKernelVirtualQuery(reinterpret_cast<const void*>(page), 1, &next, sizeof(next));
            LogFormat("[NorthstarPS4]   next mapping: query=0x%x %p-%p committed=%u name=%.32s\n", nextQuery,
                next.unk01, next.unk02, next.isCommitted, next.name);
            return false;
        }
    }
    return true;
}

// The game's free flexible memory, which this runtime's heap, stacks and
// stub pages share with the game on a PS4.
void LogFlexibleMemory(const char* when) noexcept {
    std::size_t available = 0;
    // OpenOrbis declares the size_t* parameter as a size_t.
    const int result = sceKernelAvailableFlexibleMemorySize(reinterpret_cast<std::size_t>(&available));
    LogFormat("[NorthstarPS4] free flexible memory %s: %zu KiB (0x%x)\n", when, available / 1024, result);
}

// Thread stacks, reserved while the game is starting. Later, the game has
// taken nearly all of its flexible memory, and a thread started from the menus
// could not get a stack on a PS4: the server list never started (2026-10-07).
// A slot is reused only a second after its thread finished with it, so the
// thread has left it before another starts on it.
namespace threadstacks {
constexpr int kSlots = 12;
constexpr std::size_t kSize = 256 * 1024;
unsigned char* g_base = nullptr;
struct Slot {
    std::atomic<int> busy{0};
    std::atomic<std::uint64_t> freedAt{0};
    void* (*entry)(void*) = nullptr;
    void* argument = nullptr;
};
Slot g_slots[kSlots];

void Init() noexcept {
    if (g_base) return;
    void* area = mmap(nullptr, kSlots * kSize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (area != MAP_FAILED) g_base = static_cast<unsigned char*>(area);
    LogFormat("[NorthstarPS4] thread stacks %s (%d x %zu KiB)\n", g_base ? "reserved" : "unavailable", kSlots,
        kSize / 1024);
}

void* Run(void* raw) {
    auto* slot = static_cast<Slot*>(raw);
    void* result = slot->entry(slot->argument);
    slot->freedAt.store(sceKernelGetProcessTime(), std::memory_order_relaxed);
    slot->busy.store(0, std::memory_order_release);
    return result;
}
} // namespace threadstacks

// scePthreadCreate for this runtime's threads, on a reserved stack when one is
// free and otherwise on one the system allocates.
int StartRuntimeThread(OrbisPthread* thread, void* (*entry)(void*), void* argument, const char* name) noexcept {
    using namespace threadstacks;
    const std::uint64_t now = sceKernelGetProcessTime();
    for (int i = 0; g_base && i < kSlots; ++i) {
        Slot& slot = g_slots[i];
        const std::uint64_t freedAt = slot.freedAt.load(std::memory_order_relaxed);
        if (freedAt && now - freedAt < 1000000) continue;
        int expected = 0;
        if (!slot.busy.compare_exchange_strong(expected, 1, std::memory_order_acquire)) continue;
        slot.entry = entry;
        slot.argument = argument;
        OrbisPthreadAttr attr{};
        scePthreadAttrInit(&attr);
        scePthreadAttrSetstack(&attr, g_base + i * kSize, kSize);
        const int result = scePthreadCreate(thread, &attr, Run, &slot, name);
        scePthreadAttrDestroy(&attr);
        if (result == 0) return 0;
        slot.busy.store(0, std::memory_order_release);
        LogFormat("[NorthstarPS4] thread %s not started on a reserved stack: 0x%x\n", name, result);
        break;
    }
    const int result = scePthreadCreate(thread, RuntimeThreadAttr(), entry, argument, name);
    if (result != 0) {
        LogFormat("[NorthstarPS4] thread %s not started: 0x%x\n", name, result);
        LogFlexibleMemory("then");
    }
    return result;
}

// True for `<stem>.prx` and `<stem>.sprx`.
bool ModuleNamed(const char* name, const char* stem) noexcept {
    const std::size_t length = std::strlen(stem);
    return name && std::strncmp(name, stem, length) == 0 &&
        (std::strcmp(name + length, ".prx") == 0 || std::strcmp(name + length, ".sprx") == 0);
}

namespace branchstubs {
// `jmp qword [rip+0]` and the target: 14 bytes, in 16-byte slots.
constexpr std::size_t kSlot = 16;
constexpr std::size_t kPage = 0x4000;
constexpr int kMaxPages = 32;
struct Page {
    std::uintptr_t base;
    std::size_t capacity;  // a page of its own, or a run of padding in a module
    std::size_t used;
};
Page g_pages[kMaxPages]{};
int g_pageCount = 0;
std::atomic_flag g_busy = ATOMIC_FLAG_INIT;
bool g_mapFailedLogged = false;

bool Reaches(std::uintptr_t from, std::uintptr_t to) noexcept {
    const auto distance = static_cast<std::int64_t>(to) - static_cast<std::int64_t>(from);
    // A margin for the patched instruction's own length.
    return distance > -0x7fff0000LL && distance < 0x7fff0000LL;
}

// A page of our own near `site`: mapped read-write, then made executable.
// Without MAP_FIXED the kernel takes the hint as where to start looking, so
// the search starts as far below the game's modules as a rel32 reaches: the
// first page found near the engine took the address client.prx would have
// loaded at (2026-10-07). Then nearer, then above.
std::uintptr_t MapNear(std::uintptr_t site) noexcept {
    for (int attempt = 0; attempt < 224; ++attempt) {
        const int side = attempt < 112 ? 0 : 1;
        const std::uintptr_t step = side == 0 ? 112 - attempt : attempt - 111;
        const std::uintptr_t offset = step * 0x01000000;
        if (side == 0 && site < offset + 0x10000) continue;
        const std::uintptr_t hint = ((side ? site + offset : site - offset)) & ~(kPage - 1);
        void* page = mmap(reinterpret_cast<void*>(hint), kPage, PROT_READ | PROT_WRITE,
            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (page == MAP_FAILED || page == nullptr) continue;
        const auto address = reinterpret_cast<std::uintptr_t>(page);
        if (!Reaches(site, address) || !Reaches(site, address + kPage)) {
            munmap(page, kPage);
            continue;
        }
        if (sceKernelMprotect(page, kPage, 7) != 0) {
            munmap(page, kPage);
            if (!g_mapFailedLogged) {
                g_mapFailedLogged = true;
                LogFormat("[NorthstarPS4] branch stubs: executable pages refused; using module padding\n");
            }
            return 0;
        }
        std::memset(page, 0xcc, kPage);
        return address;
    }
    return 0;
}

// Otherwise, padding between functions in the module that holds `site`:
// a run of at least 64 int3 bytes, which nothing executes or reads.
Page FindPadding(std::uintptr_t site) noexcept {
    OrbisKernelModule handles[256]{};
    std::size_t count = 0;
    if (sceKernelGetModuleList(handles, sizeof(handles), &count) != 0) return {};
    if (count > 256) count = 256;
    for (std::size_t i = 0; i < count; ++i) {
        OrbisKernelModuleInfo info{};
        info.size = sizeof(info);
        if (sceKernelGetModuleInfo(handles[i], &info) != 0) continue;
        for (std::uint32_t s = 0; s < info.segmentCount && s < 4; ++s) {
            const auto start = reinterpret_cast<std::uintptr_t>(info.segmentInfo[s].address);
            const auto end = start + info.segmentInfo[s].size;
            if (site < start || site >= end || !(info.segmentInfo[s].prot & 4)) continue;
            // The first unclaimed run within reach.
            const auto* bytes = reinterpret_cast<const std::uint8_t*>(start);
            std::size_t run = 0;
            for (std::uintptr_t a = start; a < end; ++a) {
                run = bytes[a - start] == 0xcc ? run + 1 : 0;
                // 64 bytes from an aligned start at least 16 bytes into the run.
                if (run < 96) continue;
                const std::uintptr_t base = (a - run + 1 + 31) & ~std::uintptr_t(kSlot - 1);
                bool claimed = false;
                for (int p = 0; p < g_pageCount; ++p)
                    claimed |= base < g_pages[p].base + g_pages[p].capacity && g_pages[p].base < base + 64;
                if (claimed || !Reaches(site, base)) { run = 0; continue; }
                return {base, 64, 0};
            }
        }
    }
    return {};
}

bool Write(std::uintptr_t slot, std::uintptr_t target, bool ownPage) noexcept {
    std::uint8_t code[kSlot] = {0xff, 0x25, 0, 0, 0, 0};
    std::memcpy(code + 6, &target, sizeof(target));
    code[14] = 0xcc;
    code[15] = 0xcc;
    if (ownPage) {
        std::memcpy(reinterpret_cast<void*>(slot), code, sizeof(code));
        return true;
    }
    const auto first = slot & ~std::uintptr_t(kPage - 1);
    const auto span = ((slot + kSlot - 1) & ~std::uintptr_t(kPage - 1)) - first + kPage;
    if (sceKernelMprotect(reinterpret_cast<void*>(first), span, 7) != 0) return false;
    std::memcpy(reinterpret_cast<void*>(slot), code, sizeof(code));
    return sceKernelMprotect(reinterpret_cast<void*>(first), span, 5) == 0;
}
} // namespace branchstubs

// Where a 5-byte call or jump (or a rip-relative lea) whose next instruction
// is at `next` should point to continue at `target`: the target itself when in
// range (always, under shadPS4), or a stub near the game code that jumps on to
// it. Returns 0 when neither exists; the caller's range check then refuses.
std::uintptr_t Reachable(std::uintptr_t next, std::uintptr_t target) noexcept {
    using namespace branchstubs;
#if !defined(NORTHSTAR_PS4_FORCE_BRANCH_STUBS)
    if (Reaches(next, target)) return target;
#endif
    while (g_busy.test_and_set(std::memory_order_acquire)) {}
    std::uintptr_t result = 0;
    // One stub per target serves every site within reach of it.
    for (int p = 0; p < g_pageCount && !result; ++p) {
        for (std::size_t off = 0; off < g_pages[p].used; off += kSlot) {
            const auto slot = g_pages[p].base + off;
            std::uintptr_t existing;
            std::memcpy(&existing, reinterpret_cast<const void*>(slot + 6), sizeof(existing));
            if (existing == target && Reaches(next, slot)) {
                result = slot;
                break;
            }
        }
    }
    for (int p = 0; p < g_pageCount && !result; ++p) {
        Page& page = g_pages[p];
        if (page.used + kSlot > page.capacity || !Reaches(next, page.base)) continue;
        if (Write(page.base + page.used, target, page.capacity == kPage)) {
            result = page.base + page.used;
            page.used += kSlot;
        }
    }
    if (!result && g_pageCount < kMaxPages) {
        Page page{};
        if (const auto mapped = g_mapFailedLogged ? 0 : MapNear(next)) page = {mapped, kPage, 0};
        else page = FindPadding(next);
        if (page.base && Write(page.base, target, page.capacity == kPage)) {
            page.used = kSlot;
            g_pages[g_pageCount++] = page;
            result = page.base;
            LogFormat("[NorthstarPS4] branch stubs: %s at %p for site %p\n",
                page.capacity == kPage ? "page" : "padding", reinterpret_cast<void*>(page.base),
                reinterpret_cast<void*>(next));
        }
    }
    g_busy.clear(std::memory_order_release);
    if (!result) LogFormat("[NorthstarPS4] branch stubs: nothing within reach of %p\n", reinterpret_cast<void*>(next));
    return result;
}
