// Script compile errors (PC squirrel.cpp ScriptCompileErrorHook).
//
// A mod script that fails to compile while a CLIENT VM is being set up left
// the PS4 game on a black loading screen with a spinner indefinitely (for
// example a mod installed without a mod it depends on). PC logs the
// error with the mod that owns the file and, when the error is fatal, runs
//
//   disconnect "Encountered CLIENT script compilation error, see console for details."
//
// so the player lands back in the menus with a message. The PS4 client's
// handler (client+0x67e7b0) prints through the VM's print function, which
// this module already captures (runtime_script_print.inl):
//
//   CLIENT SCRIPT COMPILE ERROR: <message>
//    -> <source line>
//   <file> line [<n>] column [<m>]
//
// PC knows fatality from how the compiler was created. Here an error is
// treated as fatal when it arrives before that context's VM has started (its
// lifecycle callback has not run yet): that is the scripts.rson compile at VM
// creation, while compilestring() at run time comes later. The reason names
// the file and mod, since a PS4 player has no console to look at. A UI error
// is only logged; there is nothing to disconnect from.
//
// The command is queued as the client's ProcessStringCmd queues a server's:
// Cbuf_AddText (engine+0x203f30) on the command buffer at engine+0x3e74280,
// under the mutex at engine+0x3e78fb0 (PLT lock +0x128, unlock +0x108).
namespace scripterrors {
constexpr std::uintptr_t kQueueSequenceVa = 0x155fbe;
constexpr std::uintptr_t kCbufLockVa = 0x3e78fb0;
constexpr std::uintptr_t kCbufVa = 0x3e74280;
constexpr std::uintptr_t kCbufAddTextVa = 0x203f30;
constexpr std::uintptr_t kMutexLockVa = 0x128;
constexpr std::uintptr_t kMutexUnlockVa = 0x108;
const char* g_pendingLabel = nullptr;  // context of a compile error awaiting its file line

bool QueueEngineCommand(const char* text) noexcept {
    const std::uintptr_t engine = netfixes::g_engineBase;
    if (!engine) return false;
    // lea r14, [lock]; mov rdi, r14; call lock; lea rdi, [cbuf]; xor edx, edx;
    // mov rsi, rbx; call Cbuf_AddText; mov rdi, r14; call unlock
    constexpr std::uint8_t sequence[] = {0x4c, 0x8d, 0x35, 0xeb, 0x2f, 0xd2, 0x03, 0x4c, 0x89, 0xf7, 0xe8, 0x5b, 0xa1,
        0xea, 0xff, 0x48, 0x8d, 0x3d, 0xac, 0xe2, 0xd1, 0x03, 0x31, 0xd2, 0x48, 0x89, 0xde, 0xe8, 0x52, 0xdf, 0x0a, 0x00,
        0x4c, 0x89, 0xf7, 0xe8, 0x22, 0xa1, 0xea, 0xff};
    if (std::memcmp(reinterpret_cast<const void*>(engine + kQueueSequenceVa), sequence, sizeof(sequence)) != 0) {
        LogFormat("[NorthstarPS4] command queue refused: engine profile mismatch\n");
        return false;
    }
    using MutexFn = int (*)(void*);
    using AddTextFn = void (*)(void*, const char*, int);
    void* const lock = reinterpret_cast<void*>(engine + kCbufLockVa);
    reinterpret_cast<MutexFn>(engine + kMutexLockVa)(lock);
    reinterpret_cast<AddTextFn>(engine + kCbufAddTextVa)(reinterpret_cast<void*>(engine + kCbufVa), text, 0);
    reinterpret_cast<MutexFn>(engine + kMutexUnlockVa)(lock);
    return true;
}

const VmLifecycle* LifecycleForLabel(const char* label) noexcept {
    if (!std::strcmp(label, "CLIENT")) return &g_runtimeClientLifecycle;
    if (!std::strcmp(label, "UI")) return &g_runtimeUiLifecycle;
    if (!std::strcmp(label, "SERVER")) return &g_runtimeServerLifecycle;
    return nullptr;
}

// The folder of the mod whose files include scripts/vscripts/<file>, or null.
bool OwningMod(const char* file, char* name, std::size_t capacity) noexcept {
    char path[320], normalized[256];
    std::snprintf(path, sizeof(path), "scripts/vscripts/%s", file);
    const ModOverlay* overlay = CurrentModOverlay();
    if (!overlay || !NormalizeRequestedPath(path, normalized, sizeof(normalized))) return false;
    const int root = FindModFileRoot(*overlay, normalized);
    if (root < 0 || static_cast<std::size_t>(root) >= overlay->dirs.size()) return false;
    const std::string& dir = overlay->dirs[static_cast<std::size_t>(root)];
    const std::size_t slash = dir.find_last_of('/');
    std::snprintf(name, capacity, "%s", dir.c_str() + (slash == std::string::npos ? 0 : slash + 1));
    return true;
}
} // namespace scripterrors

// The freeze itself: after printing, the client's compile-error handler calls
// the script-error routine (client+0x67e5b0) with fatal = 1
// (`mov esi, 1` at +0x67e8b7). For fatal errors that routine ends in
// Error("<file>: CLIENT SCRIPT COMPILE ERROR: ..."), logged as FatalError,
// which stops the main thread on PS4 with nothing on screen. With 0 it takes
// the path run-time script errors take: the "There was a problem processing
// game logic" dialog and a return to the menus. PC Northstar replaces the
// compile-error handler outright, so its engine never escalates either.
// server.prx has the same handler and call (`mov esi, 1` at +0x630267, its
// routine at +0x62ff60), patched once server.prx is mapped.
constexpr std::uintptr_t kClientCompileErrorFatalArgVa = 0x67e8b7;
constexpr std::uintptr_t kServerCompileErrorFatalArgVa = 0x630267;

void InstallRecoverableCompileErrorsAt(std::uintptr_t base, std::uintptr_t va, const std::uint8_t (&store)[7],
    const char* module) noexcept {
    // mov esi, 1; mov [rip+...], rax (the error text); mov rdi, [rbx+0x50];
    // add rdi, 0x4371; call <script-error routine>
    std::uint8_t site[28] = {0xbe, 0x01, 0x00, 0x00, 0x00};
    std::memcpy(site + 5, store, sizeof(store));
    constexpr std::uint8_t tail[] = {0x48, 0x8b, 0x7b, 0x50, 0x48, 0x81, 0xc7, 0x71, 0x43, 0x00, 0x00, 0xe8, 0xdd, 0xfc,
        0xff, 0xff};
    std::memcpy(site + 12, tail, sizeof(tail));
    if (!base || std::memcmp(reinterpret_cast<const void*>(base + va), site, sizeof(site)) != 0) {
        LogFormat("[NorthstarPS4] recoverable compile errors refused: %s profile mismatch\n", module);
        return;
    }
    constexpr std::uint8_t notFatal = 0x00;
    if (WriteEngineCode(base + va + 1, &notFatal, 1))
        LogFormat("[NorthstarPS4] recoverable compile errors installed (%s)\n", module);
    else
        LogFormat("[NorthstarPS4] recoverable compile errors: %s write failed\n", module);
}

void InstallRecoverableCompileErrors(std::uintptr_t clientBase) noexcept {
    constexpr std::uint8_t store[7] = {0x48, 0x89, 0x05, 0x05, 0x4b, 0xee, 0x01};
    InstallRecoverableCompileErrorsAt(clientBase, kClientCompileErrorFatalArgVa, store, "client");
}

void InstallServerRecoverableCompileErrors(std::uintptr_t serverBase) noexcept {
    constexpr std::uint8_t store[7] = {0x48, 0x89, 0x05, 0x75, 0xae, 0xe8, 0x00};
    InstallRecoverableCompileErrorsAt(serverBase, kServerCompileErrorFatalArgVa, store, "server");
}

void NoteScriptOutput(const char* label, const char* text) noexcept {
    using namespace scripterrors;
    if (!label || !text) return;
    if (std::strstr(text, " SCRIPT COMPILE ERROR: ")) {
        g_pendingLabel = label;
        return;
    }
    if (!g_pendingLabel || std::strcmp(g_pendingLabel, label) != 0) return;
    char file[200];
    int line = 0, column = 0;
    if (std::sscanf(text, "%199s line [%d] column [%d]", file, &line, &column) != 3) return;
    g_pendingLabel = nullptr;
    char mod[128];
    const bool modded = OwningMod(file, mod, sizeof(mod));
    LogFormat("[NorthstarPS4] %s belongs to %s\n", file, modded ? mod : "the game (Vanilla)");
    const VmLifecycle* state = LifecycleForLabel(label);
    const bool fatal = state && state->owner && !state->started;
    if (!fatal) return;
    if (!std::strcmp(label, "UI")) {
        LogFormat("[NorthstarPS4] UI script compilation error is fatal; the menus may not work\n");
        return;
    }
    char command[512];
    std::snprintf(command, sizeof(command),
        "disconnect \"Encountered %s script compilation error in %s (%s). Disable or fix that mod.\"", label, file,
        modded ? mod : "game scripts");
    if (QueueEngineCommand(command))
        LogFormat("[NorthstarPS4] %s script compilation error is fatal; disconnecting\n", label);
}
