#include "northstar_ps4/host_options.h"
#include <cstdio>
#include <cstdlib>
using namespace northstar::ps4::hostoptions;

static void Check(bool ok, const char* what, int line) {
    if (!ok) {
        std::fprintf(stderr, "line %d: %s\n", line, what);
        std::exit(1);
    }
}
#define CHECK(x) Check((x), #x, __LINE__)

int main() {
    const std::string text = Serialize({{"ns_server_name", "My = PS4\nserver"}, {"ns_server_password", ""},
        {"rcon_password", "no"}, {"ns_report_server_to_masterserver", "0"}});
    CHECK(text == "ns_server_name=My = PS4 server\nns_server_password=\nns_report_server_to_masterserver=0\n");
    auto values = Parse(text);
    CHECK(values.size() == 3);
    CHECK(values[0].first == "ns_server_name" && values[0].second == "My = PS4 server");
    CHECK(values[1].first == "ns_server_password" && values[1].second.empty());
    CHECK(values[2].second == "0");

    values = Parse("sv_cheats=1\r\nns_server_desc=a\r\n\r\nno equals\nns_server_desc=b");
    CHECK(values.size() == 1);
    CHECK(values[0].first == "ns_server_desc" && values[0].second == "b");
    CHECK(Parse("").empty());
    CHECK(IsHostOption("ns_auth_allow_insecure") && !IsHostOption("everything_unlocked"));

    auto saved = Parse("ns_server_name=Old\nns_server_password=x\n");
    Set(saved, "ns_server_name", "New\nname");
    Set(saved, "ns_report_server_to_masterserver", "0");
    CHECK(Serialize(saved) == "ns_server_name=New name\nns_server_password=x\nns_report_server_to_masterserver=0\n");

    std::puts("host_options tests passed");
    return 0;
}
