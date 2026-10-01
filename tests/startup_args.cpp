#include "northstar_ps4/startup_args.h"
#include <cstdio>
#include <cstdlib>
using namespace northstar::ps4::startup;

static void Check(bool ok, const char* what, int line) {
    if (!ok) {
        std::fprintf(stderr, "line %d: %s\n", line, what);
        std::exit(1);
    }
}
#define CHECK(x) Check((x), #x, __LINE__)

int main() {
    auto args = SplitArgs("-allowdupeaccounts +ns_server_name \"My PS4 server\"\r\n+ns_server_password secret -allowlocalhttp");
    CHECK(args.size() == 6);
    CHECK(args[0] == "-allowdupeaccounts" && args[2] == "My PS4 server" && args[4] == "secret");
    auto sets = ConVarAssignments(args);
    CHECK(sets.size() == 2);
    CHECK(sets[0].first == "ns_server_name" && sets[0].second == "My PS4 server");
    CHECK(sets[1].first == "ns_server_password" && sets[1].second == "secret");

    CHECK(ConVarAssignments(SplitArgs("+ns_server_desc \"\"")).size() == 1);
    CHECK(ConVarAssignments(SplitArgs("+ns_server_desc \"\""))[0].second.empty());
    CHECK(ConVarAssignments(SplitArgs("+map -dev")).empty());
    CHECK(ConVarAssignments(SplitArgs("+map +other 1")).size() == 1);
    CHECK(ConVarAssignments(SplitArgs("+map +other 1"))[0].first == "other");
    CHECK(ConVarAssignments(SplitArgs("+hostport 37016"))[0].second == "37016");
    CHECK(ConVarAssignments(SplitArgs("+some_offset -5"))[0].second == "-5");
    CHECK(ConVarAssignments(SplitArgs("+alone")).empty());
    CHECK(SplitArgs("   ").empty());

    {
        std::string value;
        const auto args = SplitArgs("-allowdupeaccounts -maxfoldersize 1048576 +ns_server_name x");
        if (!ArgValue(args, "-maxfoldersize", value) || value != "1048576") return std::puts("ArgValue value"), 1;
        if (ArgValue(args, "-allowdupeaccounts", value)) return std::puts("ArgValue followed by option"), 1;
        if (ArgValue(SplitArgs("-maxfoldersize"), "-maxfoldersize", value)) return std::puts("ArgValue at end"), 1;
        if (ArgValue(args, "-missing", value)) return std::puts("ArgValue absent"), 1;
    }
    std::puts("startup_args tests passed");
    return 0;
}
