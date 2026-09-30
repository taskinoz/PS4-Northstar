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
// The Host Options keyboard (below) uses the same system keyboard.
bool g_textInputOpen = false;

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
    if (At<int (*)(void*, const char*, void*, const char*)>(vm, 0x685cf0)(vm, "NS_PreSendMessage", &function, nullptr) < 0)
        return false;
    auto push = At<void (*)(void*, std::uint64_t, void*)>(vm, 0x6875f0);
    auto root = reinterpret_cast<const Object*>(static_cast<char*>(vm) + 0xb8);
    push(vm, function.tag, reinterpret_cast<void*>(function.value));
    push(vm, root->tag, reinterpret_cast<void*>(root->value));
    String(vm, text);
    Boolean(vm, true);
    Boolean(vm, isTeam);
    const int result = At<int (*)(void*, int, int, int)>(vm, 0x6876c0)(vm, 4, 0, 1);
    Pop(vm, 1);
    return result >= 0;
}

// NSPS4_OpenChatKeyboard( bool isTeam ): opens the system keyboard; false if
// one is already open or the system refused.
int OpenChatKeyboard(void* vm) {
    if (Arg(vm, 1).tag != 0x1000008) return Error(vm, "NSPS4_OpenChatKeyboard expects bool isTeam");
    if (g_chatKeyboardOpen || g_textInputOpen) { Boolean(vm, false); return 1; }
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

namespace uiapi {

// Text for other menus (the private lobby's Host Options: server name,
// description, password), on the same system keyboard. One keyboard is open
// at a time, chat's or this one.
constexpr std::uint32_t kTextInputMaxLength = 255;
constexpr std::uint32_t kImeOptionPassword = 4;
char16_t g_textInput[kTextInputMaxLength + 1];
char16_t g_textInputTitle[64];
std::uint32_t g_textInputLength = 0;
std::string g_textInputResult;

// NSPS4_OpenTextInput( string title, string text, int maxLength, bool secret ):
// opens the system keyboard holding `text`; `secret` hides what is typed. False
// if a keyboard is already open or the system refused.
int OpenTextInput(void* vm) {
    const char* title = TextArg(vm, 1);
    const char* text = TextArg(vm, 2);
    if (!title || !text || Arg(vm, 3).tag != kSqInteger || Arg(vm, 4).tag != 0x1000008)
        return Error(vm, "NSPS4_OpenTextInput expects string title, string text, int maxLength, bool secret");
    if (g_chatKeyboardOpen || g_textInputOpen) { Boolean(vm, false); return 1; }
    const auto requested = static_cast<std::int64_t>(Arg(vm, 3).value);
    g_textInputLength = requested < 1 ? 1 : requested > kTextInputMaxLength ? kTextInputMaxLength
                                                                          : static_cast<std::uint32_t>(requested);
    std::memset(g_textInput, 0, sizeof(g_textInput));
    std::memset(g_textInputTitle, 0, sizeof(g_textInputTitle));
    const std::u16string initial = chat::Utf8ToUtf16(text, std::strlen(text));
    std::memcpy(g_textInput, initial.data(), std::min<std::size_t>(initial.size(), g_textInputLength) * sizeof(char16_t));
    const std::u16string heading = chat::Utf8ToUtf16(title, std::strlen(title));
    std::memcpy(g_textInputTitle, heading.data(),
        std::min<std::size_t>(heading.size(), sizeof(g_textInputTitle) / sizeof(char16_t) - 1) * sizeof(char16_t));
    std::int32_t user = 0;
    if (sceUserServiceGetInitialUser(&user) < 0) user = 0;
    ImeDialogParam param{};
    param.userId = user;
    // A hidden (password) field takes the Basic Latin layout only; the system
    // refuses it on the default one (0x80bc0030, invalid parameter).
    const bool secret = Arg(vm, 4).value != 0;
    param.type = secret ? 1 : 0;
    param.enterLabel = 0;            // default ("Done")
    param.option = secret ? kImeOptionPassword : 0;
    param.maxTextLength = g_textInputLength;
    param.inputTextBuffer = g_textInput;
    param.posx = 960.0f;
    param.posy = 540.0f;
    param.horizontalAlignment = 1;
    param.verticalAlignment = 1;
    param.title = g_textInputTitle;
    const std::int32_t result = sceImeDialogInit(&param, nullptr);
    g_textInputOpen = result >= 0;
    if (!g_textInputOpen) LogFormat("[NorthstarPS4] text keyboard refused: 0x%x\n", static_cast<unsigned>(result));
    Boolean(vm, g_textInputOpen);
    return 1;
}

// NSPS4_UpdateTextInput(): 0 no keyboard, 1 open, 2 finished (the text is in
// NSPS4_GetTextInput), 3 cancelled. Closes the keyboard when it finishes.
int UpdateTextInput(void* vm) {
    if (!g_textInputOpen) { Integer(vm, 0); return 1; }
    const std::int32_t status = sceImeDialogGetStatus();
    if (status == kImeStatusRunning) { Integer(vm, 1); return 1; }
    ImeDialogResult result{};
    const bool finished = status == kImeStatusFinished && sceImeDialogGetResult(&result) >= 0 &&
        result.endStatus == kImeEndOk;
    sceImeDialogTerm();
    g_textInputOpen = false;
    g_textInput[kTextInputMaxLength] = 0;
    g_textInputResult = finished ? Utf16ToUtf8(g_textInput) : std::string();
    Integer(vm, finished ? 2 : 3);
    return 1;
}

// NSPS4_GetTextInput(): the text the last keyboard finished with.
int GetTextInput(void* vm) {
    String(vm, g_textInputResult.c_str());
    return 1;
}

} // namespace uiapi
