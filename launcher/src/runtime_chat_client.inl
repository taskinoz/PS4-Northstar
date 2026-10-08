// Chat on the receiving client: PC's clientchathooks.cpp and localchatwriter.cpp.
//
// PC hooks CHudChat::AddGameLine (client.dll 0x22E580) and hands each message
// to CLIENT script CHudChat_ProcessMessageStartThread, which runs mods'
// OnReceivedSayTextMessage callbacks and draws the line itself through
// NSChatWrite, NSChatWriteRaw and NSChatWriteLine. Messages a server sends with
// NSBroadcastMessage (announcements, whispers) set the high bit of the sender
// index, which vanilla code rejects; only the script path shows them.
//
// On PS4 AddGameLine is inlined into the SayText user-message handler
// (client+0x1db690). The handler reads the sender index (r14), the text
// (rbp-0x5d0), isTeam (byte at rbp-0x5d9) and isDead (dword at rbp-0x5e0),
// runs the mute checks, and at client+0x1db91b starts the loop over chat
// panels that is AddGameLine. That load of the panel list becomes a jump to
// ClientChatReceiveStub: when the script takes the message the handler
// returns, otherwise the loop runs as before.
//
// Chat panels, as PC's CHudChat (PS4 offsets, from the inlined code):
//   +0x2bc same-team colour, +0x2c0 enemy colour, +0x2c4 main text colour,
//   +0x2c8 network name colour, +0x2d8 context (0 network, 1 game),
//   +0x2e8 rich-text panel, +0x2f0 next panel; the list head is at
//   client+0x10b1be0.
// Rich-text methods, as PC: InsertChar 0x758, InsertString 0x768 (UTF-16 on
// PS4), InsertColorChange 0x7e8 (colour packed r,g,b,a), InsertFade 0x810.
//
// Included after runtime_concommands.inl, for WriteEngineCode.

namespace uiapi {

constexpr std::uintptr_t kChatHudListVa = 0x10b1be0;
constexpr std::uintptr_t kChatSettingsVa = 0x10b1cc0;
constexpr std::uintptr_t kChatFadeSustainVa = 0x10b1d50;
constexpr std::uintptr_t kChatFadeLengthVa = 0x10b1de0;
constexpr std::uintptr_t kChatSayTextHandlerVa = 0x1db690;
constexpr std::uintptr_t kChatReceiveHookVa = 0x1db91b;
constexpr std::uintptr_t kChatReceiveLoopVa = 0x1db92b;
constexpr std::uintptr_t kChatReceiveExitVa = 0x1dbc71;
constexpr std::size_t kChatHudSameTeamColor = 0x2bc;
constexpr std::size_t kChatHudEnemyTeamColor = 0x2c0;
constexpr std::size_t kChatHudMainTextColor = 0x2c4;
constexpr std::size_t kChatHudNetworkNameColor = 0x2c8;
constexpr std::size_t kChatHudContext = 0x2d8;
constexpr std::size_t kChatHudRichText = 0x2e8;
constexpr std::size_t kChatHudNext = 0x2f0;
constexpr std::size_t kRichTextInsertChar = 0x758;
constexpr std::size_t kRichTextInsertString = 0x768;
constexpr std::size_t kRichTextInsertColorChange = 0x7e8;
constexpr std::size_t kRichTextInsertFade = 0x810;
constexpr unsigned kChatCustomIndexBit = 0x80;
constexpr unsigned kChatCustomIndexMask = 0x7f;

bool g_clientChatReady = false;

template <typename Fn>
Fn RichTextMethod(void* richText, std::size_t offset) {
    return reinterpret_cast<Fn>((*reinterpret_cast<std::uintptr_t* const*>(richText))[offset / sizeof(std::uintptr_t)]);
}

// Calls fn(hud, richText) for each chat panel in the context.
template <typename Fn>
void ForEachChatHud(int context, Fn fn) {
    if (!g_clientChatReady) return;
    for (auto hud = *reinterpret_cast<char**>(g_runtimeClientBase + kChatHudListVa); hud;
         hud = *reinterpret_cast<char**>(hud + kChatHudNext)) {
        if (*reinterpret_cast<const std::int32_t*>(hud + kChatHudContext) != context) continue;
        if (void* richText = *reinterpret_cast<void**>(hud + kChatHudRichText)) fn(hud, richText);
    }
}

float ChatFadeValue(std::uintptr_t va) {
    auto var = *reinterpret_cast<const char* const*>(g_runtimeClientBase + va);
    return var ? *reinterpret_cast<const float*>(var + 0x58) : 0.0f;
}

// PC: LocalChatWriter.
struct HudChatWriter {
    int context;

    void InsertDefaultFade() {
        float sustain = 0.0f;
        float length = 0.0f;
        auto settings = *reinterpret_cast<const char* const*>(g_runtimeClientBase + kChatSettingsVa);
        if (settings && *reinterpret_cast<const std::int32_t*>(settings + 0x5c) != 0) {
            sustain = ChatFadeValue(kChatFadeSustainVa);
            length = ChatFadeValue(kChatFadeLengthVa);
        }
        ForEachChatHud(context, [&](char*, void* richText) {
            RichTextMethod<void (*)(void*, float, float)>(richText, kRichTextInsertFade)(richText, sustain, length);
        });
    }

    void InsertChar(char16_t ch) {
        ForEachChatHud(context, [&](char*, void* richText) {
            RichTextMethod<void (*)(void*, std::uint32_t)>(richText, kRichTextInsertChar)(richText, ch);
        });
        if (ch != u'\n') InsertDefaultFade();
    }

    void Text(const char* text, std::size_t length) {
        // PC logs what it writes, too.
        std::size_t visible = 0;
        while (visible < length && std::isspace(static_cast<unsigned char>(text[visible]))) ++visible;
        if (visible < length) LogFormat("[NorthstarPS4] chat: %.*s\n", static_cast<int>(length), text);
        const std::u16string wide = chat::Utf8ToUtf16(text, length);
        ForEachChatHud(context, [&](char*, void* richText) {
            RichTextMethod<void (*)(void*, const char16_t*)>(richText, kRichTextInsertString)(richText, wide.c_str());
        });
        InsertDefaultFade();
    }

    void Color(chat::ChatColor color) {
        const std::uint32_t packed = color.Packed();
        ForEachChatHud(context, [&](char*, void* richText) {
            RichTextMethod<void (*)(void*, std::uint32_t)>(richText, kRichTextInsertColorChange)(richText, packed);
        });
    }

    void Swatch(chat::ChatSwatch swatch) {
        std::size_t offset = kChatHudMainTextColor;
        switch (swatch) {
        case chat::ChatSwatch::MainText: offset = kChatHudMainTextColor; break;
        case chat::ChatSwatch::SameTeamName: offset = kChatHudSameTeamColor; break;
        case chat::ChatSwatch::EnemyTeamName: offset = kChatHudEnemyTeamColor; break;
        case chat::ChatSwatch::NetworkName: offset = kChatHudNetworkNameColor; break;
        }
        ForEachChatHud(context, [&](char* hud, void* richText) {
            const std::uint32_t color = *reinterpret_cast<const std::uint32_t*>(hud + offset);
            RichTextMethod<void (*)(void*, std::uint32_t)>(richText, kRichTextInsertColorChange)(richText, color);
        });
    }
};

// NSChatWrite( int context, string text ): text with ANSI colour escapes.
int ChatWrite(void* vm) {
    const char* text = TextArg(vm, 2);
    if (Arg(vm, 1).tag != 0x5000002 || !text) return Error(vm, "NSChatWrite expects int context, string text");
    HudChatWriter writer{static_cast<int>(Arg(vm, 1).value)};
    chat::WriteChatText(text, writer);
    return 0;
}

// NSChatWriteRaw( int context, string text ): text as it is.
int ChatWriteRaw(void* vm) {
    const char* text = TextArg(vm, 2);
    if (Arg(vm, 1).tag != 0x5000002 || !text) return Error(vm, "NSChatWriteRaw expects int context, string text");
    HudChatWriter writer{static_cast<int>(Arg(vm, 1).value)};
    writer.Text(text, std::strlen(text));
    return 0;
}

// NSChatWriteLine( int context, string text ): a new line in the main colour.
int ChatWriteLine(void* vm) {
    const char* text = TextArg(vm, 2);
    if (Arg(vm, 1).tag != 0x5000002 || !text) return Error(vm, "NSChatWriteLine expects int context, string text");
    HudChatWriter writer{static_cast<int>(Arg(vm, 1).value)};
    writer.InsertChar(u'\n');
    writer.Swatch(chat::ChatSwatch::MainText);
    chat::WriteChatText(text, writer);
    return 0;
}

// CHudChat_ProcessMessageStartThread( int playerIndex, string message,
// bool isTeam, bool isDead, int messageType ) in the CLIENT VM.
bool CallClientChatScript(int playerIndex, const char* text, bool isTeam, bool isDead, int type) noexcept {
    void* owner = g_runtimeClientLifecycle.owner;
    void* vm = owner ? *reinterpret_cast<void**>(static_cast<char*>(owner) + 8) : nullptr;
    if (!vm) return false;
    Object function{};
    if (At<int (*)(void*, const char*, void*, const char*)>(vm, 0x685cf0)(vm, "CHudChat_ProcessMessageStartThread",
            &function, nullptr) < 0)
        return false;
    auto push = At<void (*)(void*, std::uint64_t, void*)>(vm, 0x6875f0);
    auto root = reinterpret_cast<const Object*>(static_cast<char*>(vm) + 0xb8);
    push(vm, function.tag, reinterpret_cast<void*>(function.value));
    push(vm, root->tag, reinterpret_cast<void*>(root->value));
    Integer(vm, playerIndex);
    String(vm, text);
    Boolean(vm, isTeam);
    Boolean(vm, isDead);
    Integer(vm, type);
    const int result = At<int (*)(void*, int, int, int)>(vm, 0x6876c0)(vm, 6, 0, 1);
    Pop(vm, 1);
    return result >= 0;
}

// PC: h_CHudChat__AddGameLine. True when the message is dealt with (shown by
// the script, or dropped as empty), false to let the vanilla loop show it.
bool ClientChatReceived(unsigned inboxId, char* message, bool isTeam, int isDead) noexcept {
    if (!message || !*message) return true;
    const unsigned senderId = inboxId & kChatCustomIndexMask;
    const bool isCustom = senderId == 0 || (inboxId & kChatCustomIndexBit) != 0;
    // Custom messages carry their type in the first byte.
    int type = 0;
    const char* payload = message;
    if (isCustom) {
        type = message[0];
        payload = message + 1;
    }
    chat::RemoveAsciiControlSequences(message, true);
    const char* p = payload;
    while (std::isspace(static_cast<unsigned char>(*p))) p++;
    if (!*p) return true;
    return CallClientChatScript(static_cast<int>(senderId) - 1, payload, isTeam, isDead != 0, type);
}

} // namespace uiapi

extern "C" {
__attribute__((used)) std::uintptr_t g_chatHudListAddress = 0;
__attribute__((used)) std::uintptr_t g_chatReceiveLoop = 0;
__attribute__((used)) std::uintptr_t g_chatReceiveExit = 0;
__attribute__((used)) bool (*g_chatReceive)(unsigned, char*, bool, int) = nullptr;
}

// Replaces `mov r12, [list]; test r12, r12; je exit` (16 bytes) at
// client+0x1db91b. The stack is 16-byte aligned there, and only callee-saved
// registers (rbx, r12-r15, rbp) are live.
__attribute__((naked)) void ClientChatReceiveStub() {
    asm volatile(
        "movl %r14d, %edi\n\t"
        "leaq -0x5d0(%rbp), %rsi\n\t"
        "movzbl -0x5d9(%rbp), %edx\n\t"
        "movl -0x5e0(%rbp), %ecx\n\t"
        "callq *g_chatReceive(%rip)\n\t"
        "testb %al, %al\n\t"
        "jnz 1f\n\t"
        "movq g_chatHudListAddress(%rip), %r12\n\t"
        "movq (%r12), %r12\n\t"
        "testq %r12, %r12\n\t"
        "jz 1f\n\t"
        "jmpq *g_chatReceiveLoop(%rip)\n\t"
        "1:\n\t"
        "jmpq *g_chatReceiveExit(%rip)\n\t");
}

bool ClientChatReceivedThunk(unsigned inboxId, char* message, bool isTeam, int isDead) noexcept {
    return uiapi::ClientChatReceived(inboxId, message, isTeam, isDead);
}

void InstallClientChat(std::uintptr_t clientBase, std::size_t clientSpan) noexcept {
    // The handler, the hook site, and the inlined code that pins the panel
    // offsets, the fade settings and the rich-text method slots.
    constexpr std::uint8_t handlerBytes[] = {0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54,
        0x53, 0x48, 0x81, 0xec, 0xc8, 0x05, 0x00, 0x00};
    constexpr std::uint8_t hookBytes[] = {0x4c, 0x8b, 0x25, 0xbe, 0x62, 0xed, 0x00, 0x4d, 0x85, 0xe4, 0x0f, 0x84,
        0x46, 0x03, 0x00, 0x00};
    constexpr std::uint8_t contextBytes[] = {0x41, 0x83, 0xbc, 0x24, 0xd8, 0x02, 0x00, 0x00, 0x01};
    constexpr std::uint8_t colorBytes[] = {0x49, 0x8d, 0x84, 0x24, 0xbc, 0x02, 0x00, 0x00};
    constexpr std::uint8_t fadeBytes[] = {0x49, 0x8b, 0xbc, 0x24, 0xe8, 0x02, 0x00, 0x00, 0x48, 0x8b, 0x0d, 0x2f,
        0x61, 0xed, 0x00, 0xc5, 0xf8, 0x57, 0xc0, 0xc5, 0xf0, 0x57, 0xc9, 0x48, 0x8b, 0x07, 0x83, 0x79, 0x5c, 0x00,
        0x48, 0x8b, 0x80, 0x10, 0x08, 0x00, 0x00, 0x74, 0x18, 0x48, 0x8b, 0x0d, 0xa0, 0x61, 0xed, 0x00, 0x48, 0x8b,
        0x15, 0x29, 0x62, 0xed, 0x00, 0xc5, 0xfa, 0x10, 0x41, 0x58, 0xc5, 0xfa, 0x10, 0x4a, 0x58, 0xff, 0xd0, 0x49,
        0x8b, 0xbc, 0x24, 0xe8, 0x02, 0x00, 0x00, 0x41, 0x8b, 0xb4, 0x24, 0xc4, 0x02, 0x00, 0x00, 0x48, 0x8b, 0x07,
        0xff, 0x90, 0xe8, 0x07, 0x00, 0x00};
    constexpr std::uint8_t nextBytes[] = {0x4d, 0x8b, 0xa4, 0x24, 0xf0, 0x02, 0x00, 0x00, 0x4d, 0x85, 0xe4, 0x0f,
        0x85, 0xdf, 0xfc, 0xff, 0xff, 0x48, 0x8b, 0x05, 0x08, 0x6b, 0x8c, 0x00, 0x48, 0x8b, 0x00};
    const auto hook = clientBase + uiapi::kChatReceiveHookVa;
    const auto distance = static_cast<std::int64_t>(Reachable(hook + 5, reinterpret_cast<std::uintptr_t>(&ClientChatReceiveStub))) -
        static_cast<std::int64_t>(hook + 5);
    if (!clientBase ||
        !ValidateEnginePreimage(clientBase, clientSpan, uiapi::kChatSayTextHandlerVa, handlerBytes, sizeof(handlerBytes)) ||
        !ValidateEnginePreimage(clientBase, clientSpan, uiapi::kChatReceiveHookVa, hookBytes, sizeof(hookBytes)) ||
        !ValidateEnginePreimage(clientBase, clientSpan, 0x1db950, contextBytes, sizeof(contextBytes)) ||
        !ValidateEnginePreimage(clientBase, clientSpan, 0x1dba49, colorBytes, sizeof(colorBytes)) ||
        !ValidateEnginePreimage(clientBase, clientSpan, 0x1dbb82, fadeBytes, sizeof(fadeBytes)) ||
        !ValidateEnginePreimage(clientBase, clientSpan, 0x1dbc60, nextBytes, sizeof(nextBytes)) ||
        distance < -2147483648LL || distance > 2147483647LL) {
        LogFormat("[NorthstarPS4] client chat hook refused: client profile mismatch\n");
        return;
    }
    uiapi::g_clientChatReady = true;  // the writer's layout is confirmed
    g_chatHudListAddress = clientBase + uiapi::kChatHudListVa;
    g_chatReceiveLoop = clientBase + uiapi::kChatReceiveLoopVa;
    g_chatReceiveExit = clientBase + uiapi::kChatReceiveExitVa;
    g_chatReceive = &ClientChatReceivedThunk;
    std::uint8_t jump[sizeof(hookBytes)];
    std::memset(jump, 0x90, sizeof(jump));
    jump[0] = 0xe9;
    const auto rel = static_cast<std::int32_t>(distance);
    std::memcpy(jump + 1, &rel, sizeof(rel));
    if (!WriteEngineCode(hook, jump, sizeof(jump))) {
        LogFormat("[NorthstarPS4] client chat hook failed: mprotect\n");
        return;
    }
    LogFormat("[NorthstarPS4] client chat hook installed\n");
}
