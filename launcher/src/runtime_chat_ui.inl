// Typing chat on a pad: the system on-screen keyboard (sceImeDialog).
//
// PC types chat into the HUD's text box, and the engine's ClientSayText hook
// hands the text to CLIENT script NS_PreSendMessage, which runs mods'
// OnPreSendMessage callbacks and then NSSendMessage. A PS4 has no keyboard, so
// Northstar.PS4 opens the system keyboard from a menu (ui/ps4_chat_menu.nut)
// and the text takes the same route from there: NS_PreSendMessage in the CLIENT
// VM. Never through a console command, where a `;` in a message would run the
// rest as commands.
//
// Included after runtime_ui_callbacks.inl, for the CLIENT VM's owner.

namespace uiapi {

constexpr std::uint32_t kChatMaxLength = 127;
constexpr std::int32_t kImeStatusRunning = 1;
constexpr std::int32_t kImeStatusFinished = 2;
constexpr std::int32_t kImeEndOk = 0;

// The layouts the OS reads (shadPS4 core/libraries/ime/ime_common.h): text is
// UTF-16 whatever this toolchain's wchar_t is.
struct ImeDialogParam {
    std::int32_t userId;
    std::uint32_t type;
    std::uint64_t supportedLanguages;
    std::uint32_t enterLabel;
    std::uint32_t inputMethod;
    void* filter;
    std::uint32_t option;
    std::uint32_t maxTextLength;
    char16_t* inputTextBuffer;
    float posx;
    float posy;
    std::uint32_t horizontalAlignment;
    std::uint32_t verticalAlignment;
    const char16_t* placeholder;
    const char16_t* title;
    std::int8_t reserved[16];
};
struct ImeDialogResult {
    std::int32_t endStatus;
    std::int8_t reserved[12];
};

extern "C" {
std::int32_t sceImeDialogInit(const ImeDialogParam* param, void* extended);
std::int32_t sceImeDialogGetStatus();
std::int32_t sceImeDialogGetResult(ImeDialogResult* result);
std::int32_t sceImeDialogTerm();
std::int32_t sceUserServiceGetInitialUser(std::int32_t* userId);
}

char16_t g_chatText[kChatMaxLength + 1];
bool g_chatKeyboardOpen = false;
bool g_chatKeyboardTeam = false;

std::string Utf16ToUtf8(const char16_t* text) {
    std::string out;
    for (std::size_t i = 0; text[i]; ++i) {
        std::uint32_t cp = text[i];
        if (cp >= 0xd800 && cp <= 0xdbff && text[i + 1] >= 0xdc00 && text[i + 1] <= 0xdfff) {
            cp = 0x10000 + ((cp - 0xd800) << 10) + (text[i + 1] - 0xdc00);
            ++i;
        }
        if (cp < 0x80) {
            out += static_cast<char>(cp);
        } else if (cp < 0x800) {
            out += static_cast<char>(0xc0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3f));
        } else if (cp < 0x10000) {
            out += static_cast<char>(0xe0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3f));
            out += static_cast<char>(0x80 | (cp & 0x3f));
        } else {
            out += static_cast<char>(0xf0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3f));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3f));
            out += static_cast<char>(0x80 | (cp & 0x3f));
        }
    }
    return out;
}

// NS_PreSendMessage( string message, bool isIngame, bool isTeam ) in the CLIENT
// VM, or false when there is none (not in a match) or it is missing.
bool CallPreSendMessage(const char* text, bool isTeam) noexcept {
    void* owner = g_runtimeClientLifecycle.owner;
    void* vm = owner ? *reinterpret_cast<void**>(static_cast<char*>(owner) + 8) : nullptr;
    if (!vm) return false;
    Object function{};
    if (At<int (*)(void*, const char*, void*, const char*)>(0x685cf0)(vm, "NS_PreSendMessage", &function, nullptr) < 0)
        return false;
    auto push = At<void (*)(void*, std::uint64_t, void*)>(0x6875f0);
    auto root = reinterpret_cast<const Object*>(static_cast<char*>(vm) + 0xb8);
    push(vm, function.tag, reinterpret_cast<void*>(function.value));
    push(vm, root->tag, reinterpret_cast<void*>(root->value));
    String(vm, text);
    Boolean(vm, true);
    Boolean(vm, isTeam);
    const int result = At<int (*)(void*, int, int, int)>(0x6876c0)(vm, 4, 0, 1);
    Pop(vm, 1);
    return result >= 0;
}

// NSPS4_OpenChatKeyboard( bool isTeam ): opens the system keyboard; false if
// one is already open or the system refused.
int OpenChatKeyboard(void* vm) {
    if (Arg(vm, 1).tag != 0x1000008) return Error(vm, "NSPS4_OpenChatKeyboard expects bool isTeam");
    if (g_chatKeyboardOpen) { Boolean(vm, false); return 1; }
    std::int32_t user = 0;
    if (sceUserServiceGetInitialUser(&user) < 0) user = 0;
    std::memset(g_chatText, 0, sizeof(g_chatText));
    static const char16_t kChat[] = u"Chat";
    static const char16_t kTeamChat[] = u"Team chat";
    ImeDialogParam param{};
    param.userId = user;
    param.type = 0;                  // default: any text
    param.enterLabel = 1;            // "Send"
    param.maxTextLength = kChatMaxLength;
    param.inputTextBuffer = g_chatText;
    param.posx = 960.0f;
    param.posy = 540.0f;
    param.horizontalAlignment = 1;   // centre
    param.verticalAlignment = 1;
    g_chatKeyboardTeam = Arg(vm, 1).value != 0;
    param.title = g_chatKeyboardTeam ? kTeamChat : kChat;
    const std::int32_t result = sceImeDialogInit(&param, nullptr);
    g_chatKeyboardOpen = result >= 0;
    if (!g_chatKeyboardOpen) LogFormat("[NorthstarPS4] chat keyboard refused: 0x%x\n", static_cast<unsigned>(result));
    Boolean(vm, g_chatKeyboardOpen);
    return 1;
}

// NSPS4_UpdateChatKeyboard(): 0 no keyboard, 1 open, 2 message sent,
// 3 cancelled or empty. Sends and closes the keyboard when it finishes.
int UpdateChatKeyboard(void* vm) {
    if (!g_chatKeyboardOpen) { Integer(vm, 0); return 1; }
    const std::int32_t status = sceImeDialogGetStatus();
    if (status == kImeStatusRunning) { Integer(vm, 1); return 1; }
    ImeDialogResult result{};
    const bool finished = status == kImeStatusFinished && sceImeDialogGetResult(&result) >= 0 &&
        result.endStatus == kImeEndOk;
    sceImeDialogTerm();
    g_chatKeyboardOpen = false;
    const std::string text = finished ? Utf16ToUtf8(g_chatText) : std::string();
    if (text.empty() || text.find_first_not_of(" \t") == std::string::npos) { Integer(vm, 3); return 1; }
    if (!CallPreSendMessage(text.c_str(), g_chatKeyboardTeam) && !SendChat(text.c_str(), g_chatKeyboardTeam))
        LogFormat("[NorthstarPS4] chat not sent: not in a match\n");
    Integer(vm, 2);
    return 1;
}

} // namespace uiapi
