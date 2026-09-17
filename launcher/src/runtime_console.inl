// Source console support.
//
// The console is not stripped from this build. `client.prx` carries
// `CGameConsole`, `CGameConsoleDialog`, `CConsoleDialog`, `CConsolePanel` and
// `IConsoleDisplayFunc`, exposes them as `GameConsole004`, and holds both the
// `toggleconsole` command and the `con_enable` convar; `engine.prx` has
// `Con_Init()` and `Con_Shutdown()`. Northstar's own `autoexec_ns_client.cfg`
// already binds a key to `toggleconsole`, and this port already serves it.
//
// What the build does not have is any way to type: `sceKeyboard` and `sceIme`
// appear in none of the modules, so neither a USB keyboard nor the system's
// on-screen keyboard can reach the guest. Commands therefore need a path that
// does not involve typing, and that path needs one primitive - run a command
// string.
//
// **What is known so far.** `IVEngineClient` is registered by engine.prx as
// `VEngineClient013`, and dumping its vtable at runtime (the probe below) gives
// 48 entries. Slot 26 (`engine+0x470a0`) is `ServerCmd`: it formats its
// argument with `"cmd %s"` into a 255-byte buffer, builds a `CCommand` on the
// stack, tokenizes it at `+0x204b70` and hands it to `+0xf0870`. It is the only
// slot in the interface that touches either.
//
//   void CCommand::Tokenize(CCommand* self, const char* text)   engine +0x204b70
//
// `Tokenize` zeroes the CCommand itself (`[0]`, `[8]`, `[0x10]`), so a caller
// only supplies the storage, and it resolves its break set through its own
// rip-relative reference, so nothing here has to locate or initialise engine
// data. The break set is `{}()':`, characters absent from an ordinary command.
//
// `+0xf0870` is **not** local execution, which is what the `cmd ` prefix should
// have suggested from the start: ServerCmd ships a command to the server. Both
// probes agree - `echo` produced no output, and `exec` of a missing file
// produced no filesystem request whatsoever, which is precisely what a network
// send does with nothing connected. Local execution needs `Cbuf_AddText` or
// `Cmd_ExecuteString`, neither of which is identified yet; engine.prx has
// `Cbuf_Init()` and `Cbuf_Shutdown()` as profiler scope strings but no
// `Unknown command` string to anchor the dispatcher.
constexpr const char* kEngineClientInterface = "VEngineClient013";
constexpr int kEngineClientVtableDump = 48;
constexpr std::uintptr_t kCCommandTokenizeVa = 0x204b70;
constexpr std::uintptr_t kCommandDispatchVa = 0xf0870;
// ServerCmd builds its CCommand in 0x730 bytes of stack; this rounds up.
constexpr std::size_t kCCommandSize = 0x800;
constexpr const char* kConsoleCommandFile = "/data/northstar_ps4/console.txt";
constexpr std::size_t kConsoleCommandFileMax = 16 * 1024;

// Off until the *local* execution function is found. `+0xf0870` was assumed to
// be a local dispatcher because ServerCmd calls it right after tokenizing, but
// executing `exec <missing>.cfg` through it produced no filesystem request at
// all, and `echo` produced no output. That is exactly what a network send looks
// like when nothing is connected: ServerCmd's job is to ship the command to the
// server, not to run it here. The tokenizer is still right; the second half is
// not. What is missing is `Cbuf_AddText`/`Cmd_ExecuteString`.
constexpr bool kConsoleCommandsEnabled = false;

using CCommandTokenizeFn = void (*)(void*, const char*);
using CommandDispatchFn = void (*)(void*);

std::uintptr_t g_consoleEngineBase = 0;
bool g_consoleCommandsReady = false;

// Runs one command exactly as the engine runs a line from a cfg.
bool ExecuteEngineCommand(const char* text) noexcept {
    if (!kConsoleCommandsEnabled || !g_consoleCommandsReady || !text || !*text) return false;
    auto tokenize = reinterpret_cast<CCommandTokenizeFn>(g_consoleEngineBase + kCCommandTokenizeVa);
    auto dispatch = reinterpret_cast<CommandDispatchFn>(g_consoleEngineBase + kCommandDispatchVa);
    alignas(16) unsigned char command[kCCommandSize];
    std::memset(command, 0, sizeof(command));
    tokenize(command, text);
    dispatch(command);
    LogFormat("[NorthstarPS4] console command: %s\n", text);
    return true;
}

void ProbeEngineClientInterface(OrbisKernelModule engineHandle,
    std::uintptr_t engineBase, std::size_t engineSize) noexcept {
    // The prologues of the two primitives, so a different build is refused
    // rather than called at the wrong address.
    constexpr std::uint8_t tokenizePreimage[] = {
        0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56, 0x41,
        0x55, 0x41, 0x54, 0x53, 0x48, 0x83, 0xec, 0x78, 0x48};
    constexpr std::uint8_t dispatchPreimage[] = {
        0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56, 0x41,
        0x54, 0x53, 0x48, 0x81, 0xec, 0x10, 0x04, 0x00, 0x00};
    if (!ValidateEnginePreimage(engineBase, engineSize, kCCommandTokenizeVa,
            tokenizePreimage, sizeof(tokenizePreimage)) ||
        !ValidateEnginePreimage(engineBase, engineSize, kCommandDispatchVa,
            dispatchPreimage, sizeof(dispatchPreimage))) {
        LogFormat("[NorthstarPS4] console commands refused: primitive preimage mismatch\n");
        return;
    }
    g_consoleEngineBase = engineBase;
    g_consoleCommandsReady = true;
    LogFormat("[NorthstarPS4] console command execution enabled=%d\n", kConsoleCommandsEnabled ? 1 : 0);
    LogFormat("[NorthstarPS4] console commands ready tokenize=engine+%lx dispatch=engine+%lx\n",
        kCCommandTokenizeVa, kCommandDispatchVa);

    // The interface itself is only read, to keep the vtable dump available for
    // identifying further entry points.
    using CreateInterfaceFn = void* (*)(const char*, int*);
    void* createInterfaceAddress = nullptr;
    if (sceKernelDlsym(static_cast<std::int32_t>(engineHandle), "CreateInterface",
            &createInterfaceAddress) != 0 || !createInterfaceAddress) {
        return;
    }
    auto createInterface = reinterpret_cast<CreateInterfaceFn>(createInterfaceAddress);
    int status = 0;
    void* engineClient = createInterface(kEngineClientInterface, &status);
    LogFormat("[NorthstarPS4] console probe: %s=%p status=%d\n",
        kEngineClientInterface, engineClient, status);
    if (!engineClient) return;
    auto** vtable = *reinterpret_cast<void***>(engineClient);
    if (!vtable) return;
    for (int i = 0; i < kEngineClientVtableDump; ++i) {
        const auto entry = reinterpret_cast<std::uintptr_t>(vtable[i]);
        if (entry >= engineBase && entry < engineBase + engineSize)
            LogFormat("[NorthstarPS4] console vtable[%d] = engine+%lx\n", i, entry - engineBase);
    }
}

// Commands queued from outside the game: each line of the file is run, and the
// file is truncated so the same batch is not replayed. This is what stands in
// for typing, since the build has no keyboard path at all.
void DrainConsoleCommandFile() noexcept {
    if (!g_consoleCommandsReady) return;
    FILE* file = std::fopen(kConsoleCommandFile, "rb");
    if (!file) return;
    static char text[kConsoleCommandFileMax];
    const std::size_t count = std::fread(text, 1, sizeof(text) - 1, file);
    std::fclose(file);
    text[count] = '\0';
    if (count == 0) return;
    // Truncate first: a command that takes the process down must not be able to
    // replay itself on the next boot.
    if (FILE* clear = std::fopen(kConsoleCommandFile, "wb")) std::fclose(clear);
    char* line = text;
    while (*line) {
        char* end = line;
        while (*end && *end != '\n' && *end != '\r') ++end;
        const char terminator = *end;
        *end = '\0';
        while (*line == ' ' || *line == '\t') ++line;
        if (*line && *line != '/') ExecuteEngineCommand(line);
        if (!terminator) break;
        line = end + 1;
    }
}

// One-shot proof that the path works, run the first time a VM lifecycle
// finishes. `echo` is no use as a probe because its output goes to the console
// buffer rather than stdout, so nothing observable reaches the log. `exec` of a
// file that does not exist is harmless and *is* observable: the engine asks the
// filesystem for it, and this module's own open hook logs the attempt. Seeing
// that request is proof the command was dispatched and not merely called.
constexpr const char* kConsoleSelfTestCfg = "northstar_ps4_console_probe.cfg";

void RunConsoleSelfTest() noexcept {
    static bool done = false;
    if (done || !g_consoleCommandsReady) return;
    done = true;
    char command[128];
    std::snprintf(command, sizeof(command), "exec %s", kConsoleSelfTestCfg);
    ExecuteEngineCommand(command);
}
