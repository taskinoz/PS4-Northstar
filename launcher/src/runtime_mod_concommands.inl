// mod.json "ConCommands" (PC: modmanager.cpp ModConCommandCallback, parsing in
// mod_concommands.h).
//
// Each enabled mod's commands are registered with the engine's ConCommand
// constructor, the same one the port's native commands use. Running one calls
// its Function in the UI, CLIENT or SERVER VM, as PC does:
//   - no arguments: `Function()`;
//   - otherwise `Function( array<string> args )`, without the command's name.
// A command whose VM does not exist (CLIENT or SERVER outside a match) only
// logs, as PC warns. Commands of a mod a reload disables stay registered, as
// on PC, but do nothing, because only enabled mods' commands are looked up.
// Command objects and names are never freed: the engine's command list points
// at them.

namespace modconcommands {

struct Entry {
    std::string name;
    std::string function;
    mods::ScriptContext context;
};

// Pointers, not objects: this module runs no static constructors.
std::vector<Entry>* g_enabled = nullptr;
std::vector<std::string>* g_registered = nullptr;

const char* ContextName(mods::ScriptContext context) {
    switch (context) {
        case mods::ScriptContext::Ui: return "UI";
        case mods::ScriptContext::Client: return "CLIENT";
        case mods::ScriptContext::Server: return "SERVER";
        default: return "INVALID";
    }
}

void* ContextVm(mods::ScriptContext context) {
    void* owner = nullptr;
    switch (context) {
        case mods::ScriptContext::Ui: owner = g_runtimeUiLifecycle.owner; break;
        case mods::ScriptContext::Client: owner = g_runtimeClientLifecycle.owner; break;
        case mods::ScriptContext::Server: owner = g_runtimeServerLifecycle.owner; break;
        default: break;
    }
    return owner ? *reinterpret_cast<void**>(static_cast<char*>(owner) + 8) : nullptr;
}

void Run(const Entry& entry, const CCommandView* command) noexcept {
    void* vm = ContextVm(entry.context);
    if (!vm) {
        LogFormat("[NorthstarPS4] ConCommand `%s` was called while the associated Squirrel VM `%s` was unloaded\n",
            entry.name.c_str(), ContextName(entry.context));
        return;
    }
    using namespace uiapi;
    Object function{};
    if (At<int (*)(void*, const char*, void*, const char*)>(vm, 0x685cf0)(vm, entry.function.c_str(), &function,
            nullptr) < 0) {
        LogFormat("[NorthstarPS4] ConCommand `%s`: %s function %s not found\n", entry.name.c_str(),
            ContextName(entry.context), entry.function.c_str());
        return;
    }
    auto push = At<void (*)(void*, std::uint64_t, void*)>(vm, 0x6875f0);
    auto root = reinterpret_cast<const Object*>(static_cast<char*>(vm) + 0xb8);
    push(vm, function.tag, reinterpret_cast<void*>(function.value));
    push(vm, root->tag, reinterpret_cast<void*>(root->value));
    int params = 1;
    if (command->argc > 1) {
        Array(vm);
        for (std::int64_t i = 1; i < command->argc && i < 64; ++i) {
            String(vm, command->argv[i] ? command->argv[i] : "");
            Append(vm);
        }
        params = 2;
    }
    const int result = At<int (*)(void*, int, int, int)>(vm, 0x6876c0)(vm, params, 0, 1);
    Pop(vm, 1);
    if (result < 0)
        LogFormat("[NorthstarPS4] ConCommand `%s`: %s function %s failed\n", entry.name.c_str(),
            ContextName(entry.context), entry.function.c_str());
}

void Callback(const CCommandView* command) noexcept {
    if (!command || command->argc < 1 || !command->argv[0] || !g_enabled) return;
    const char* name = command->argv[0];
    for (const auto& entry : *g_enabled)
        if (entry.name == name) {
            Run(entry, command);
            return;
        }
}

} // namespace modconcommands

// At startup (RegisterNativeConCommands) and after every reload.
void RegisterModConCommands() noexcept {
    using namespace modconcommands;
    if (!g_conCommandConstruct) return;
    if (!g_enabled) g_enabled = new std::vector<Entry>();
    if (!g_registered) g_registered = new std::vector<std::string>();
    std::vector<Entry> enabled;
    static ModDiscovery discovery;
    CollectModNames(discovery);
    for (std::int32_t i = 0; i < discovery.count; ++i) {
        static char json[kModJsonBufferSize];
        char path[kModDirCapacity + 16];
        std::size_t size = 0;
        std::snprintf(path, sizeof(path), "%s/mod.json", discovery.dirs[i]);
        if (!ReadFileIntoBuffer(path, json, sizeof(json), size)) continue;
        std::vector<std::string> warnings;
        const auto commands = mods::ParseModConCommands(json, &warnings);
        for (const auto& warning : warnings) LogFormat("[NorthstarPS4] %s: %s\n", discovery.names[i], warning.c_str());
        for (const auto& command : commands) {
            bool known = false;
            for (const auto& entry : enabled) known = known || entry.name == command.name;
            if (known) continue; // PC: the first enabled mod with the name answers
            enabled.push_back({command.name, command.function, command.context});
            bool registered = false;
            for (const auto& name : *g_registered) registered = registered || name == command.name;
            if (registered) continue;
            // A stock ConVar of the same name keeps its meaning (PC checks
            // FindCommand before registering).
            if (g_modConVarFindVar && g_modConVarFindVar(g_modConVarCvar, command.name.c_str())) {
                LogFormat("[NorthstarPS4] mod concommand %s not registered: the name is taken\n", command.name.c_str());
                continue;
            }
            auto* object = new (std::nothrow) std::uint8_t[0x60]();
            char* name = new (std::nothrow) char[command.name.size() + 1];
            char* help = new (std::nothrow) char[command.help.size() + 1];
            if (!object || !name || !help) return;
            std::memcpy(name, command.name.c_str(), command.name.size() + 1);
            std::memcpy(help, command.help.c_str(), command.help.size() + 1);
            g_conCommandConstruct(object, name, &Callback, help, command.flags, nullptr);
            g_registered->push_back(command.name);
            LogFormat("[NorthstarPS4] mod concommand registered: %s -> %s %s flags=0x%x (%s)\n", name,
                ContextName(command.context), command.function.c_str(), command.flags, discovery.names[i]);
        }
    }
    g_enabled->swap(enabled);
}
