#include "northstar_ps4/banlist.h"
#include <cstdio>
#include <cstdlib>
using namespace northstar::ps4::bans;

static void Check(bool ok, const char* what, int line) {
    if (!ok) {
        std::fprintf(stderr, "line %d: %s\n", line, what);
        std::exit(1);
    }
}
#define CHECK(x) Check((x), #x, __LINE__)

int main() {
    const std::string text = "# server bans\r\n1001\n  1002 # banned for being unfunny\n\n\t1003\t\n#1004\n";
    auto uids = Parse(text);
    CHECK(uids.size() == 3 && uids[0] == 1001 && uids[1] == 1002 && uids[2] == 1003);
    CHECK(IsBanned(text, 1002) && !IsBanned(text, 1004) && !IsBanned(text, 0));

    CHECK(Ban("", 5) == "5\n");
    CHECK(Ban("1001", 5) == "1001\n5\n");
    CHECK(Parse(Ban(text, 42)).back() == 42);

    const std::string unbanned = Unban(text, 1002, "2026-10-01 12:34");
    CHECK(!IsBanned(unbanned, 1002) && IsBanned(unbanned, 1001) && IsBanned(unbanned, 1003));
    CHECK(unbanned.find("#   1002 # banned for being unfunny # unban date: 2026-10-01 12:34\n") != std::string::npos);
    CHECK(Unban(text, 99, "x") == "# server bans\n1001\n  1002 # banned for being unfunny\n\n\t1003\t\n#1004\n");

    std::puts("banlist tests passed");
    return 0;
}
