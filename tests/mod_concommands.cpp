#include "northstar_ps4/mod_concommands.h"
#include <cstdio>
#include <cstdlib>
using namespace northstar::ps4::mods;

static void Check(bool ok, const char* what, int line) {
    if (!ok) {
        std::fprintf(stderr, "line %d: %s\n", line, what);
        std::exit(1);
    }
}
#define CHECK(x) Check((x), #x, __LINE__)

int main() {
    std::vector<std::string> warnings;
    auto commands = ParseModConCommands(R"({
        "Name": "Example",
        "ConCommands": [
            { "Name": "ex_ui", "Function": "Ex_Ui", "Context": "UI", "HelpString": "Opens it" },
            { "Name": "ex_server", "Function": "Ex_Server", "Context": "SERVER", "Flags": "CHEAT | SERVER_CAN_EXECUTE" },
            { "Name": "ex_int", "Function": "Ex_Int", "Context": "CLIENT", "Flags": 16384 },
            { "Name": "ex_bad_flag", "Function": "F", "Context": "CLIENT", "Flags": "CHEAT|NOT_A_FLAG" },
            { "Name": "no_function", "Context": "UI" },
            { "Name": "no_context", "Function": "F" },
            { "Name": "bad_context", "Function": "F", "Context": "MENU" },
            { "Function": "F", "Context": "UI" },
        ]
    })", &warnings);
    CHECK(commands.size() == 4);
    CHECK(commands[0].name == "ex_ui" && commands[0].function == "Ex_Ui" && commands[0].context == ScriptContext::Ui);
    CHECK(commands[0].help == "Opens it" && commands[0].flags == 0);
    CHECK(commands[1].context == ScriptContext::Server && commands[1].flags == ((1 << 14) | (1 << 28)));
    CHECK(commands[2].context == ScriptContext::Client && commands[2].flags == 16384);
    CHECK(commands[3].flags == (1 << 14));
    CHECK(warnings.size() == 5);
    CHECK(warnings[0].find("NOT_A_FLAG") != std::string::npos);

    CHECK(ParseConVarFlagsString("UNKNOWN") == (1 << 30));
    CHECK(ParseConVarFlagsString(" PRINTABLEONLY | GAMEDLL_FOR_REMOTE_CLIENTS ") == (1 << 10));
    CHECK(ParseConVarFlagsString("") == 0);
    CHECK(ParseModConCommands(R"({"Name":"x"})").empty());
    warnings.clear();
    CHECK(ParseModConCommands(R"({"ConCommands": {}})", &warnings).empty() && warnings.size() == 1);

    std::puts("mod_concommands tests passed");
    return 0;
}
