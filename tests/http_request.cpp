#include "northstar_ps4/http_request.h"
#include <cstdio>
#include <cstdlib>
#include <string>
using namespace northstar::ps4::http;

static void Check(bool ok, const char* what, int line) {
    if (!ok) {
        std::fprintf(stderr, "line %d: %s\n", line, what);
        std::exit(1);
    }
}
#define CHECK(x) Check((x), #x, __LINE__)

static std::string Url(const std::string& url, int method, bool bodyEmpty, const Parameters& query) {
    return BuildRequestUrl(url, method, bodyEmpty, query);
}

int main() {
    CHECK(std::string(MethodName(kGet)) == "GET");
    CHECK(std::string(MethodName(kPatch)) == "PATCH");
    CHECK(std::string(MethodName(kOptions)) == "OPTIONS");
    CHECK(std::string(MethodName(9)) == "INVALID");
    CHECK(UsesPostOptions(kDelete) && !UsesPostOptions(kHead) && !UsesPostOptions(kOptions));
    CHECK(CanHaveQueryParameters(kGet) && CanHaveQueryParameters(kPut) && !CanHaveQueryParameters(kHead));

    CHECK(UrlEscape("a b&c=d~._-") == "a%20b%26c%3Dd~._-");
    CHECK(UrlEscape("\xc3\xa9/") == "%C3%A9%2F");

    CHECK(WithDefaultScheme("example.com/x") == "https://example.com/x");
    CHECK(WithDefaultScheme("http://example.com") == "http://example.com");

    CHECK(!UrlHasQuery("https://a.b/c"));
    CHECK(!UrlHasQuery("https://a.b/c?"));
    CHECK(UrlHasQuery("https://a.b/c?x=1"));
    CHECK(!UrlHasQuery("https://a.b/c#frag?x"));

    const Parameters query = {{"q", {"hello world"}}, {"tag", {"a", "b"}}};
    CHECK(Url("https://a.b/s", kGet, true, query) == "https://a.b/s?q=hello%20world&tag=a&tag=b");
    CHECK(Url("https://a.b/s?x=1", kGet, true, query) == "https://a.b/s?x=1&q=hello%20world&tag=a&tag=b");
    // A POST-like request with a body keeps its query parameters out of the URL.
    CHECK(Url("https://a.b/s", kPost, false, query) == "https://a.b/s");
    // With an empty body they go in the URL, as PC's condition has it.
    CHECK(Url("https://a.b/s", kPost, true, query) == "https://a.b/s?q=hello%20world&tag=a&tag=b");
    // HEAD cannot have them, but an empty body lets them through on PC too.
    CHECK(Url("https://a.b/s", kHead, true, query) == "https://a.b/s?q=hello%20world&tag=a&tag=b");
    CHECK(Url("https://a.b/s", kHead, false, query) == "https://a.b/s");

    UrlParts parts;
    CHECK(ParseUrl("https://northstar.tf/client/servers", parts) && parts.host == "northstar.tf" && parts.port == 443 &&
          parts.scheme == "https");
    CHECK(ParseUrl("HTTP://user:pw@Example.com:8080/x?y", parts) && parts.host == "Example.com" && parts.port == 8080 &&
          parts.scheme == "http");
    CHECK(ParseUrl("http://[::1]:81/", parts) && parts.ipv6Literal && parts.host == "::1" && parts.port == 81);
    CHECK(ParseUrl("ftp://h/", parts) && parts.scheme == "ftp" && parts.port == 0);
    CHECK(!ParseUrl("https:///path", parts));
    CHECK(!ParseUrl("https://h:99999/", parts));
    CHECK(!ParseUrl("https://h:8x/", parts));

    std::uint8_t ip[4];
    CHECK(ParseIPv4("192.168.1.20", ip) && ip[0] == 192 && ip[3] == 20);
    CHECK(!ParseIPv4("192.168.1", ip));
    CHECK(!ParseIPv4("256.1.1.1", ip));
    CHECK(!ParseIPv4("example.com", ip));

    CHECK(IsPrivateIPv4(10, 1, 2, 3));
    CHECK(IsPrivateIPv4(172, 16, 0, 1) && IsPrivateIPv4(172, 31, 255, 1) && !IsPrivateIPv4(172, 32, 0, 1));
    CHECK(IsPrivateIPv4(192, 168, 0, 1) && IsPrivateIPv4(127, 0, 0, 1) && IsPrivateIPv4(169, 254, 1, 1));
    CHECK(IsPrivateIPv4(100, 64, 0, 1) && !IsPrivateIPv4(100, 128, 0, 1));
    CHECK(IsPrivateIPv4(224, 0, 0, 1) && IsPrivateIPv4(239, 255, 255, 250) && IsPrivateIPv4(250, 1, 1, 1));
    CHECK(IsPrivateIPv4(198, 18, 0, 1) && IsPrivateIPv4(203, 0, 113, 5) && IsPrivateIPv4(0, 1, 2, 3));
    CHECK(!IsPrivateIPv4(1, 1, 1, 1) && !IsPrivateIPv4(8, 8, 8, 8) && !IsPrivateIPv4(104, 21, 3, 4));

    std::puts("http_request tests passed");
    return 0;
}
