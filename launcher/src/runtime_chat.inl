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
    if (At<int (*)(void*, const char*, void*, const char*)>(vm, 0x685cf0)(vm, "CServerGameDLL_ProcessMessageStartThread",
            &function, nullptr) < 0)
        return false;
    auto push = At<void (*)(void*, std::uint64_t, void*)>(vm, 0x6875f0);
    auto root = reinterpret_cast<const Object*>(static_cast<char*>(vm) + 0xb8);
    push(vm, function.tag, reinterpret_cast<void*>(function.value));
    push(vm, root->tag, reinterpret_cast<void*>(root->value));
    Integer(vm, playerIndex);
    String(vm, text);
    Boolean(vm, isTeam);
    const int result = At<int (*)(void*, int, int, int)>(vm, 0x6876c0)(vm, 4, 0, 1);
    Pop(vm, 1);
    return result >= 0;
}

// PC: ServerLimitsManager::CheckChatLimits. Each player may send
// sv_max_chat_messages_per_sec messages (5) in a one-second window; the rest
// are dropped. PC keys the window by client and this by client slot.
struct ChatLimitWindow {
    std::uint64_t start;
    int count;
};
ChatLimitWindow g_chatLimitWindows[128]{};

bool CheckChatLimits(unsigned senderPlayerId) noexcept {
    if (!g_chatLimitConVar || senderPlayerId == 0 || senderPlayerId > 128) return true;
    auto& window = g_chatLimitWindows[senderPlayerId - 1];
    const std::uint64_t now = sceKernelGetProcessTime();  // microseconds
    if (now - window.start >= 1000000) {
        window.start = now;
        window.count = 0;
    }
    if (window.count >= *reinterpret_cast<const std::int32_t*>(static_cast<char*>(g_chatLimitConVar) + kConVarIntValueOffset))
        return false;
    ++window.count;
    return true;
}

// PC: h_CServerGameDLL__OnReceivedSayTextMessage, which cleans the text in
// place first.
void RuntimeServerSayText(void* self, unsigned senderPlayerId, const char* text, bool isTeam) noexcept {
    if (!text) return;
    chat::RemoveAsciiControlSequences(const_cast<char*>(text), true);
    const char* p = text;
    while (std::isspace(static_cast<unsigned char>(*p))) p++;
    if (!*p) return;
    if (!CheckChatLimits(senderPlayerId)) return;
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

// SayText with a custom sender: PC's ChatBroadcastMessage, behind SERVER
// NSBroadcastMessage. The sender index is 0 for an anonymous message, or the
// player's index with the high bit set; the first byte of the text is the
// message type. Vanilla code sends SayText only from a connected player, one
// message per recipient, so this builds the message the way that code does
// (server+0xaeb50, from 0xaec8e):
//   - a CRecipientFilter on the stack (vtable server+0xa30dc0), filled by
//     AddRecipient (0x14a430) and made reliable through vtable slot 3;
//   - the "SayText" user message index (lookup 0x7ef2f0 in the table at
//     0x149a7f8), then g_pEngineServer's UserMessageBegin (slot 0xc8), whose
//     bf_write goes in the message buffer global (0xac2830);
//   - WriteByte (0x591bf0), WriteString (0x591cf0) and two inlined one-bit
//     writes, then MessageEnd (slot 0xd0);
//   - the filter's destructor (0x6cada0, vtable slot 0).
// Players come from the table the vanilla loop reads: gpGlobals (the pointer
// at server+0xabd3f8) +0x34 is maxClients, and the player for index i is at
// [[gpGlobals+0x80] + i*8 + 0xe040].
constexpr std::uintptr_t kServerGlobalsSlotVa = 0xabd3f8;
constexpr std::uintptr_t kServerEngineSlotVa = 0xabd288;
constexpr std::uintptr_t kServerMessageBufferVa = 0xac2830;
constexpr std::uintptr_t kServerUserMessagesVa = 0x149a7f8;
constexpr std::uintptr_t kServerLookupUserMessageVa = 0x7ef2f0;
constexpr std::uintptr_t kServerSayTextNameVa = 0x8d8c86;
constexpr std::uintptr_t kRecipientFilterVtableVa = 0xa30dc0;
constexpr std::uintptr_t kRecipientFilterAddVa = 0x14a430;
constexpr std::uintptr_t kRecipientFilterDestructVa = 0x6cada0;
constexpr std::uintptr_t kMessageWriteByteVa = 0x591bf0;
constexpr std::uintptr_t kMessageWriteStringVa = 0x591cf0;
constexpr unsigned kCustomMessageIndexBit = 0x80;
bool g_serverBroadcastReady = false;

template <typename T>
T& ServerAt(std::uintptr_t va) { return *reinterpret_cast<T*>(g_runtimeServerBase + va); }

void* ServerPlayerByIndex(int index) noexcept {
    auto globals = ServerAt<char*>(kServerGlobalsSlotVa);
    if (!globals || index < 1 || index > *reinterpret_cast<std::int32_t*>(globals + 0x34)) return nullptr;
    auto table = *reinterpret_cast<char**>(globals + 0x80);
    return table ? *reinterpret_cast<void**>(table + static_cast<std::size_t>(index) * 8 + 0xe040) : nullptr;
}

// The vanilla code's inlined bf_write::WriteOneBit: data at +0, bit count at
// +0xc, current bit at +0x10, overflow flag at +0x14.
void MessageWriteBit(char* buffer, bool value) noexcept {
    auto& current = *reinterpret_cast<std::int32_t*>(buffer + 0x10);
    if (current >= *reinterpret_cast<std::int32_t*>(buffer + 0xc)) {
        buffer[0x14] = 1;
        return;
    }
    if (buffer[0x14]) return;
    auto data = *reinterpret_cast<std::uint8_t**>(buffer);
    const auto mask = static_cast<std::uint8_t>(1u << (current & 7));
    if (value)
        data[current >> 3] |= mask;
    else
        data[current >> 3] &= static_cast<std::uint8_t>(~mask);
    ++current;
}

bool ServerChatBroadcast(int fromPlayerIndex, int toPlayerIndex, const char* text, bool isTeam, bool isDead,
    int messageType) noexcept {
    if (!g_serverBroadcastReady || !text) return false;
    void* toPlayer = nullptr;
    if (toPlayerIndex >= 0) {
        toPlayer = ServerPlayerByIndex(toPlayerIndex + 1);
        if (!toPlayer) return true;  // as PC: no such player, nothing sent
    }
    char sendText[256];
    sendText[0] = static_cast<char>(messageType);
    std::strncpy(sendText + 1, text, 254);
    sendText[255] = 0;
    const unsigned fromPlayerId =
        fromPlayerIndex < 0 ? 0 : ((static_cast<unsigned>(fromPlayerIndex) + 1) | kCustomMessageIndexBit);

    alignas(16) char filter[0x40] = {};
    *reinterpret_cast<std::uintptr_t*>(filter) = g_runtimeServerBase + kRecipientFilterVtableVa;
    auto add = reinterpret_cast<void (*)(void*, void*)>(g_runtimeServerBase + kRecipientFilterAddVa);
    if (toPlayer) {
        add(filter, toPlayer);
    } else {
        auto globals = ServerAt<char*>(kServerGlobalsSlotVa);
        const int maxClients = globals ? *reinterpret_cast<std::int32_t*>(globals + 0x34) : 0;
        for (int i = 1; i <= maxClients; ++i)
            if (void* player = ServerPlayerByIndex(i)) add(filter, player);
    }
    auto filterTable = *reinterpret_cast<void (***)(void*)>(filter);
    filterTable[3](filter);  // MakeReliable

    const char* name = reinterpret_cast<const char*>(g_runtimeServerBase + kServerSayTextNameVa);
    const int index = reinterpret_cast<int (*)(void*, const char**)>(g_runtimeServerBase + kServerLookupUserMessageVa)(
        reinterpret_cast<void*>(g_runtimeServerBase + kServerUserMessagesVa), &name);
    void* engine = ServerAt<void*>(kServerEngineSlotVa);
    bool sent = false;
    if (index != -1 && engine) {
        auto engineTable = *reinterpret_cast<std::uintptr_t**>(engine);
        auto begin = reinterpret_cast<char* (*)(void*, void*, int, const char*, int)>(engineTable[0xc8 / 8]);
        char* buffer = begin(engine, filter, index, name, 2);
        ServerAt<char*>(kServerMessageBufferVa) = buffer;
        if (buffer) {
            reinterpret_cast<void (*)(void*, int)>(g_runtimeServerBase + kMessageWriteByteVa)(
                buffer, static_cast<int>(fromPlayerId));
            reinterpret_cast<void (*)(void*, const char*)>(g_runtimeServerBase + kMessageWriteStringVa)(buffer, sendText);
            MessageWriteBit(buffer, isTeam);
            MessageWriteBit(buffer, isDead);
            reinterpret_cast<void (*)(void*)>(engineTable[0xd0 / 8])(engine);  // MessageEnd
            sent = true;
        }
        ServerAt<char*>(kServerMessageBufferVa) = nullptr;
    }
    reinterpret_cast<void (*)(void*)>(g_runtimeServerBase + kRecipientFilterDestructVa)(filter);
    return sent;
}

struct BroadcastPreimage {
    std::uintptr_t va;
    const std::uint8_t* bytes;
    std::size_t size;
};

bool CheckServerBroadcastProfile() noexcept {
    static constexpr std::uint8_t globals[] = {0x48, 0x8b, 0x05, 0x70, 0xe8, 0xa0, 0x00, 0x8b, 0x48, 0x34};
    static constexpr std::uint8_t players[] = {0x48, 0x8b, 0x90, 0x80, 0x00, 0x00, 0x00, 0x49, 0x0f, 0xbf, 0xf4, 0x48,
        0x8b, 0x9c, 0xf2, 0x40, 0xe0, 0x00, 0x00};
    static constexpr std::uint8_t begin[] = {0x48, 0x8d, 0x0d, 0x20, 0x21, 0x98, 0x00, 0x66, 0xc7, 0x45, 0xa0, 0x00,
        0x00, 0x66, 0xc7, 0x45, 0xc8, 0x00, 0x00, 0x44, 0x89, 0xbd, 0x74, 0xff, 0xff, 0xff, 0x4c, 0x89, 0x55, 0x80, 0xc5,
        0xf8, 0x11, 0x40, 0x0c, 0xc5, 0xf8, 0x11, 0x00, 0x48, 0x89, 0x4d, 0x98, 0xe8, 0x67, 0xb7, 0x09, 0x00, 0x48, 0x8b,
        0x45, 0x98, 0x4c, 0x89, 0xef, 0xff, 0x50, 0x18, 0x48, 0x8d, 0x1d, 0xac, 0x9f, 0x82, 0x00, 0x48, 0x8d, 0x3d, 0x17,
        0xbb, 0x3e, 0x01, 0x48, 0x8d, 0x75, 0x88, 0x48, 0x89, 0x5d, 0x88, 0xe8, 0x02, 0x06, 0x74, 0x00, 0x41, 0x89, 0xc7,
        0x41, 0x83, 0xff, 0xff, 0x75, 0x11, 0x31, 0xc0, 0x48, 0x8d, 0x3d, 0xc7, 0x69, 0x7c, 0x00, 0x48, 0x89, 0xde, 0xe8,
        0x50, 0x16, 0xf5, 0xff, 0x48, 0x8b, 0x3d, 0x79, 0xe5, 0xa0, 0x00, 0x41, 0xb8, 0x02, 0x00, 0x00, 0x00, 0x4c, 0x89,
        0xee, 0x44, 0x89, 0xfa, 0x48, 0x89, 0xd9, 0x48, 0x8b, 0x07, 0xff, 0x90, 0xc8, 0x00, 0x00, 0x00};
    static constexpr std::uint8_t buffer[] = {0x4c, 0x89, 0x2d, 0xf8, 0x3a, 0xa1, 0x00};
    static constexpr std::uint8_t writeByte[] = {0x4c, 0x89, 0xef, 0x89, 0xde, 0xe8, 0x9a, 0x2e, 0x4e, 0x00};
    static constexpr std::uint8_t writeString[] = {0x48, 0x8b, 0x75, 0x80, 0x4c, 0x89, 0xef, 0xe8, 0x68, 0x2f, 0x4e,
        0x00};
    static constexpr std::uint8_t end[] = {0x48, 0x8b, 0x3d, 0x0a, 0xe4, 0xa0, 0x00, 0x48, 0x8b, 0x07, 0xff, 0x90, 0xd0,
        0x00, 0x00, 0x00, 0x48, 0x8d, 0x05, 0x72, 0xd8, 0x90, 0x00, 0x4c, 0x8d, 0x6d, 0x98, 0x48, 0xc7, 0x05, 0x93, 0x39,
        0xa1, 0x00, 0x00, 0x00, 0x00, 0x00};
    static constexpr std::uint8_t destructor[] = {0x55, 0x48, 0x89, 0xe5, 0x41, 0x56, 0x53, 0x48, 0x89, 0xfb, 0x48,
        0x8d, 0x05, 0x4f, 0x19, 0x2f, 0x00, 0x48, 0x89, 0x03};
    const BroadcastPreimage checks[] = {
        {0xaeb81, globals, sizeof(globals)},
        {0xaeba4, players, sizeof(players)},
        {0xaec99, begin, sizeof(begin)},
        {0xaed31, buffer, sizeof(buffer)},
        {0xaed4c, writeByte, sizeof(writeByte)},
        {0xaed7c, writeString, sizeof(writeString)},
        {0xaee77, end, sizeof(end)},
        {kRecipientFilterDestructVa, destructor, sizeof(destructor)},
    };
    for (const auto& check : checks)
        if (!ValidateEnginePreimage(g_runtimeServerBase, g_runtimeServerSpan, check.va, check.bytes, check.size))
            return false;
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
    g_serverBroadcastReady = CheckServerBroadcastProfile();
    LogFormat("[NorthstarPS4] server chat hook installed%s\n",
        g_serverBroadcastReady ? "" : "; NSBroadcastMessage refused: server profile mismatch");
}
