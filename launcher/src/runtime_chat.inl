// Text chat. Included inside runtime_server_vm.inl, after the persistence code.
//
// PC Northstar (primedev/client/chatcommand.cpp, server/serverchathooks.cpp,
// scripts/client/clientchathooks.cpp, client/localchatwriter.cpp) routes chat
// through script at both ends. The console game kept the PC chat code, so the
// same pieces exist on PS4:
//   - sending: engine ClientSayText (engine+0x473e0), same arguments as PC's
//     engine.dll 0x54780; `say`, `say_team` and CLIENT NSSendMessage call it
//     (runtime_concommands.inl, runtime_ui_api.inl);
//   - the server side: CServerGameDLL::OnReceivedSayTextMessage
//     (server+0xaeb50, PC server.dll 0x1595C0), reached through a vtable slot
//     at server+0x9a7d08; it ignores `this`, sends SayText to every eligible
//     player and prints "OnReceivedSayTextMessage - ...";
//   - the client side: the SayText user message handler (client+0x1db690),
//     into which PC's CHudChat::AddGameLine is inlined on PS4.

constexpr std::uintptr_t kServerSayTextVa = 0xaeb50;
constexpr std::uintptr_t kServerSayTextSlotVa = 0x9a7d08;

using ServerSayTextFn = void (*)(void* self, unsigned senderPlayerId, const char* text, bool isTeam);
ServerSayTextFn g_originalServerSayText = nullptr;
bool g_serverChatHooked = false;

// PC: serverchathooks.cpp. Player chat goes to Northstar's SERVER script,
// CServerGameDLL_ProcessMessageStartThread( int playerIndex, string message,
// bool isTeam ), which runs mods' OnReceivedSayTextMessage callbacks and then
// calls NSSendMessage to pass the message on; without the script the vanilla
// handler sends it as before.
constexpr unsigned kCustomMessageIndexMask = 0x7f;

bool CallServerChatScript(int playerIndex, const char* text, bool isTeam) noexcept {
    void* owner = g_runtimeServerLifecycle.owner;
    void* vm = owner ? *reinterpret_cast<void**>(static_cast<char*>(owner) + 8) : nullptr;
    if (!vm) return false;
    using namespace uiapi;
    Object function{};
    if (At<int (*)(void*, const char*, void*, const char*)>(0x685cf0)(vm, "CServerGameDLL_ProcessMessageStartThread",
            &function, nullptr) < 0)
        return false;
    auto push = At<void (*)(void*, std::uint64_t, void*)>(0x6875f0);
    auto root = reinterpret_cast<const Object*>(static_cast<char*>(vm) + 0xb8);
    push(vm, function.tag, reinterpret_cast<void*>(function.value));
    push(vm, root->tag, reinterpret_cast<void*>(root->value));
    Integer(vm, playerIndex);
    String(vm, text);
    Boolean(vm, isTeam);
    const int result = At<int (*)(void*, int, int, int)>(0x6876c0)(vm, 4, 0, 1);
    Pop(vm, 1);
    return result >= 0;
}

void RuntimeServerSayText(void* self, unsigned senderPlayerId, const char* text, bool isTeam) noexcept {
    if (!text || !*text) return;
    LogFormat("[NorthstarPS4] chat received from player %u%s (%zu chars)\n", senderPlayerId,
        isTeam ? ", team" : "", std::strlen(text));
    if (!CallServerChatScript(static_cast<int>(senderPlayerId) - 1, text, isTeam))
        g_originalServerSayText(self, senderPlayerId, text, isTeam);
}

// SERVER NSSendMessage( int playerIndex, string text, bool isTeam ): PC's
// ChatSendMessage, the vanilla send past the hook.
bool ServerChatSend(int playerIndex, const char* text, bool isTeam) noexcept {
    if (!g_originalServerSayText || !text) return false;
    g_originalServerSayText(nullptr, static_cast<unsigned>(playerIndex + 1) & kCustomMessageIndexMask, text, isTeam);
    return true;
}

void InstallServerChat() noexcept {
    if (g_serverChatHooked || !g_runtimeServerBase) return;
    constexpr std::uint8_t sayTextBytes[] = {0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54,
        0x53, 0x48, 0x83, 0xec, 0x78};
    auto slot = reinterpret_cast<std::uintptr_t*>(g_runtimeServerBase + kServerSayTextSlotVa);
    if (!ValidateEnginePreimage(g_runtimeServerBase, g_runtimeServerSpan, kServerSayTextVa, sayTextBytes, sizeof(sayTextBytes)) ||
        !AuthAddressReadable(slot) || *slot != g_runtimeServerBase + kServerSayTextVa) {
        LogFormat("[NorthstarPS4] server chat hook refused: server profile mismatch\n");
        g_serverChatHooked = true;  // do not retry every map
        return;
    }
    // The table sits in relocated read-only data; the page is left writable.
    void* page = reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(slot) & ~std::uintptr_t(0x3fff));
    if (sceKernelMprotect(page, 0x4000, 3) != 0) {
        LogFormat("[NorthstarPS4] server chat hook failed: mprotect\n");
        g_serverChatHooked = true;
        return;
    }
    g_originalServerSayText = reinterpret_cast<ServerSayTextFn>(*slot);
    *slot = reinterpret_cast<std::uintptr_t>(&RuntimeServerSayText);
    g_serverChatHooked = true;
    LogFormat("[NorthstarPS4] server chat hook installed\n");
}
