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

    // Sign-in pushes: the request head.
    RequestHead head;
    assert(ParseRequestHead("POST /northstar/signin HTTP/1.1\r\nHost: x\r\ncontent-LENGTH:  57\r\n", head));
    assert(head.method == "POST" && head.path == kSignInPath && head.contentLength == 57);
    assert(ParseRequestHead("GET /northstar/hello HTTP/1.0", head) && head.contentLength == 0 && head.key.empty());
    assert(ParseRequestHead("GET /northstar/hello HTTP/1.1\r\nx-NorthstarPS4-key:  0123456789abcdef0123456789abcdef \r\n",
               head) && head.key == "0123456789abcdef0123456789abcdef");

    // Whether a helper is paired: its key against the console's.
    assert(KeyMatches("0123456789abcdef0123456789abcdef", "0123456789abcdef0123456789abcdef"));
    assert(!KeyMatches("0123456789abcdef0123456789abcdee", "0123456789abcdef0123456789abcdef"));
    assert(!KeyMatches("0123", "0123456789abcdef0123456789abcdef"));
    assert(!KeyMatches("", "") && !KeyMatches("", "0123456789abcdef0123456789abcdef"));
    assert(!ParseRequestHead("GET /x", head) && !ParseRequestHead("GET x HTTP/1.1", head));
    assert(!ParseRequestHead("POST / HTTP/1.1\r\nContent-Length: 99999\r\n", head));
    assert(!ParseRequestHead("POST / HTTP/1.1\r\nContent-Length: x\r\n", head));

    // Sign-in pushes: who may replace the identity.
    IdentityFields push;
    assert(ParseIdentity(R"({"uid":"1012345678901","playerToken":"fedcba9876543210fedcba9876543210",
        "refreshUrl":"http://192.168.1.5:37011/atlas/token","refreshKey":"00112233445566778899aabbccddeeff",
        "code":"0042"})", push));
    assert(push.code == "0042");
    int wrong = 0;
    assert(CheckSignInPush(push, false, "0042", "", wrong).empty());
    assert(CheckSignInPush(push, true, "9999", "", wrong).empty());  // this machine needs no code
    IdentityFields guess = push;
    guess.code = "1234";
    assert(CheckSignInPush(guess, false, "0042", "", wrong).find("wrong code") == 0 && wrong == 1);
    assert(CheckSignInPush(guess, false, "0042", guess.refreshKey, wrong).empty());  // the paired helper
    assert(CheckSignInPush(guess, false, "", "", wrong).find("wrong code") == 0);  // no code to match
    for (int i = 0; i < kMaxWrongCodes; ++i) CheckSignInPush(guess, false, "0042", "", wrong);
    assert(CheckSignInPush(push, false, "0042", "", wrong).find("too many") == 0);  // even the right one
    assert(CheckSignInPush(push, true, "0042", "", wrong).empty());
    IdentityFields bad = push;
    bad.refreshKey.clear();
    wrong = 0;
    assert(CheckSignInPush(bad, true, "0042", "", wrong).find("malformed") != std::string::npos);
    assert(FormatSignInCode(42) == "0042" && FormatSignInCode(123456) == "3456");

    std::puts("atlas_refresh tests passed");
    return 0;
}
