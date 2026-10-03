// Host tests for the Atlas token refresh helpers (atlas_refresh.h).
#include "northstar_ps4/atlas_refresh.h"
#include <cassert>
#include <cstdio>
#include <string>

using namespace northstar::ps4::atlas;

int main() {
    const std::string token = "0123456789abcdef0123456789abcdef";
    const std::string fresh = "fedcba9876543210fedcba9876543210";

    // The identity file, with and without the helper's members.
    IdentityFields id;
    assert(ParseIdentity(R"({"uid":"1012345678901","playerToken":"0123456789abcdef0123456789abcdef"})", id));
    assert(id.uid == "1012345678901" && id.token == token && id.refreshUrl.empty() && id.refreshKey.empty());
    assert(ParseIdentity(R"({"uid":"1","playerToken":"x","refreshUrl":"http://192.168.1.20:37011/atlas/token",
        "refreshKey":"00112233445566778899aabbccddeeff","note":{"refreshUrl":"ignored"}})", id));
    assert(id.refreshUrl == "http://192.168.1.20:37011/atlas/token");
    assert(id.refreshKey == "00112233445566778899aabbccddeeff");
    assert(!ParseIdentity("not json", id));

    // Validation.
    assert(IsHex32(token) && !IsHex32("0123456789ABCDEF0123456789ABCDEF") && !IsHex32("abc"));
    assert(IsDigits("1012345678901") && !IsDigits("") && !IsDigits("12a") && !IsDigits("123456789012345678901"));
    assert(IsRefreshUrl("http://127.0.0.1:37011/atlas/token"));
    assert(IsRefreshUrl("https://pc.local/atlas/token"));
    assert(!IsRefreshUrl("ftp://pc/x") && !IsRefreshUrl("http://") && !IsRefreshUrl("http:///x"));
    assert(!IsRefreshUrl("http://pc/a b") && !IsRefreshUrl("http://pc/\"x") && !IsRefreshUrl("http://pc/x\\y"));

    // A helper reply.
    std::string reason;
    IdentityFields reply{"1012345678901", fresh, "", ""};
    assert(AcceptRefreshedToken(reply, "1012345678901", token, reason));
    assert(!AcceptRefreshedToken(reply, "999", token, reason) && reason.find("different EA account") != std::string::npos);
    assert(!AcceptRefreshedToken(reply, "1012345678901", fresh, reason) && reason.find("just refused") != std::string::npos);
    reply.token = "short";
    assert(!AcceptRefreshedToken(reply, "1012345678901", token, reason) && reason.find("unreadable") != std::string::npos);

    // Writing the file back keeps the pairing, and drops an invalid one.
    IdentityFields out{"1012345678901", fresh, "http://127.0.0.1:37011/atlas/token", "00112233445566778899aabbccddeeff"};
    const std::string json = BuildIdentityJson(out);
    IdentityFields back;
    assert(ParseIdentity(json.c_str(), back));
    assert(back.uid == out.uid && back.token == out.token && back.refreshUrl == out.refreshUrl &&
        back.refreshKey == out.refreshKey);
    out.refreshKey = "not-hex";
    assert(BuildIdentityJson(out).find("refreshUrl") == std::string::npos);

    std::puts("atlas_refresh tests passed");
    return 0;
}
