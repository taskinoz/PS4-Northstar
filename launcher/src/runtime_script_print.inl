// Squirrel script output routed into the module log.
//
// On this port `print`, `printl`, `printt` and `Msg` all reach the log as
// nothing at all. The Squirrel base library registers `print` at VA 0x6d0b30
// (its registration record is at 0xa83ea8, naming the string at 0x92ddc5), and
// that native ends in:
//
//   006d0b84  mov  rax, qword ptr [rbx + 0x50]    ; sqvm -> SQSharedState
//   006d0b88  mov  rcx, qword ptr [rax + 0x4350]  ; SQSharedState::_printfunc
//   006d0b8f  test rcx, rcx
//   006d0b92  je   0x6d0ba2                       ; null: returns silently
//   006d0b94  lea  rsi, [rip + ...]               ; "%s"
//   006d0ba0  call rcx                            ; printfunc(vm, "%s", text)
//
// The pointer is *not* null here - a first run reported `previous=0x9603fae0`,
// engine code - so script output was never being discarded, it was going to
// the engine's own console sink, which does not reach shadPS4 stdout the way
// this module's sceKernelDebugOutText output does. The hook below therefore
// tees rather than replaces: it logs the text and then forwards to whatever
// was installed, so the in-game console keeps working.
// Vanilla script funnels everything through that one native:
// `printl( text )` is `print( text + "\n" )`, `printt( ... )` joins its
// arguments and calls `printl`, and `Msg( text )` calls `print` directly.
//
// Installing a function pointer is a data write into the VM's own shared
// state, not a code patch, so no call site is rewritten and the engine's own
// `print` implementation still runs unchanged.
using ScriptPrintFn = void (*)(void*, const char*, ...);
constexpr std::size_t kSharedStatePrintFuncOffset = 0x4350;

// One entry per SQSharedState. UI and CLIENT have their own, and the label is
// captured at install time so the sink does not have to reach into the
// lifecycle globals to name the context.
//
// Every VM creation allocates a fresh shared state, so entries accumulate
// across map loads unless they are released. The first version of this sized
// the table at 4 and refused once full, which silently stopped capturing
// script output from the fourth VM onwards - the table must therefore be
// released on VM teardown (see RemoveScriptPrint, called from the destroy
// hook) and a full table evicts rather than refuses, because a stale entry
// only ever refers to a shared state that has already been freed.
constexpr std::size_t kMaxScriptPrintHooks = 16;
struct ScriptPrintHook { void* shared; ScriptPrintFn original; const char* label; };
ScriptPrintHook g_scriptPrintHooks[kMaxScriptPrintHooks]{};
std::size_t g_scriptPrintHookCount = 0;

const ScriptPrintHook* FindScriptPrintHook(void* shared) noexcept {
    for (std::size_t i = 0; i < g_scriptPrintHookCount; ++i)
        if (g_scriptPrintHooks[i].shared == shared) return &g_scriptPrintHooks[i];
    return nullptr;
}

void ScriptPrint(void* vm, const char* format, ...) noexcept {
    if (!format) return;
    char text[1024];
    va_list arguments;
    va_start(arguments, format);
    const int written = std::vsnprintf(text, sizeof(text), format, arguments);
    va_end(arguments);
    if (written <= 0) return;
    // printl appends its own newline; LogFormat adds one too.
    std::size_t length = std::strlen(text);
    while (length && (text[length - 1] == '\n' || text[length - 1] == '\r')) text[--length] = '\0';
    const ScriptPrintHook* hook = vm
        ? FindScriptPrintHook(*reinterpret_cast<void**>(static_cast<char*>(vm) + 0x50))
        : nullptr;
    if (length) LogFormat("[NorthstarPS4] [%s script] %s\n", hook ? hook->label : "?", text);
    // Preserve whatever the engine had installed, if anything ever is. The
    // original takes varargs, so the already-formatted text is passed through
    // the same "%s" shape the caller used.
    if (hook && hook->original) hook->original(vm, "%s", text);
}

bool InstallScriptPrint(void* shared, const char* label) noexcept {
    if (!shared) return false;
    // This preimage encodes both offsets the sink depends on: `mov rax,[rbx+0x50]`
    // then `mov rcx,[rax+0x4350]`. If either moved, refuse rather than write a
    // function pointer into an unknown field.
    constexpr std::uint8_t sinkBytes[] = {
        0x48,0x8b,0x43,0x50,0x48,0x8b,0x88,0x50,0x43,0x00,0x00,0x48,0x85,0xc9,0x74};
    constexpr std::uint8_t printBytes[] = {
        0x55,0x48,0x89,0xe5,0x41,0x56,0x53,0x48,0x83,0xec,0x10,0x4c,0x8b,0x35};
    if (!ValidateEnginePreimage(g_runtimeClientBase, g_runtimeClientSpan, 0x6d0b30, printBytes, sizeof(printBytes)) ||
        !ValidateEnginePreimage(g_runtimeClientBase, g_runtimeClientSpan, 0x6d0b84, sinkBytes, sizeof(sinkBytes))) {
        LogFormat("[NorthstarPS4] script print sink profile mismatch; script output stays hidden\n");
        return false;
    }
    if (FindScriptPrintHook(shared)) return true;
    auto slot = reinterpret_cast<ScriptPrintFn*>(static_cast<char*>(shared) + kSharedStatePrintFuncOffset);
    ScriptPrintFn previous = *slot;
    if (previous == &ScriptPrint) return true;
    if (g_scriptPrintHookCount >= kMaxScriptPrintHooks) {
        // Drop the oldest rather than stop capturing; it belongs to a VM that
        // has already gone away.
        LogFormat("[NorthstarPS4] script print hook table full, evicting oldest\n");
        for (std::size_t i = 1; i < kMaxScriptPrintHooks; ++i) g_scriptPrintHooks[i - 1] = g_scriptPrintHooks[i];
        --g_scriptPrintHookCount;
    }
    g_scriptPrintHooks[g_scriptPrintHookCount++] = {shared, previous, label};
    *slot = &ScriptPrint;
    LogFormat("[NorthstarPS4] %s script print installed shared=%p previous=%p\n",
        label, shared, reinterpret_cast<void*>(previous));
    return true;
}

// Releases the entry for a VM that is being torn down. Called before the
// engine frees the shared state, so the pointer is still valid here; leaving
// the entry behind would both leak a slot and risk mislabelling a later VM
// that the allocator hands the same address.
void RemoveScriptPrint(void* shared) noexcept {
    for (std::size_t i = 0; i < g_scriptPrintHookCount; ++i) {
        if (g_scriptPrintHooks[i].shared != shared) continue;
        LogFormat("[NorthstarPS4] %s script print released shared=%p\n", g_scriptPrintHooks[i].label, shared);
        for (std::size_t j = i + 1; j < g_scriptPrintHookCount; ++j) g_scriptPrintHooks[j - 1] = g_scriptPrintHooks[j];
        --g_scriptPrintHookCount;
        return;
    }
}
