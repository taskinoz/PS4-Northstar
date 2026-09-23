// Host tests for northstar_ps4/server_list.h. With a path argument, also
// parses a saved live response from https://northstar.tf/client/servers and
// checks it against an independent count of its top-level objects.
#include "northstar_ps4/server_list.h"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iterator>
using namespace northstar::ps4::mods;

static const char* kServer =
    "{\"id\":\"abc\",\"name\":\"N\",\"description\":\"D\",\"map\":\"mp_glitch\","
    "\"playlist\":\"tdm\",\"playerCount\":3,\"maxPlayers\":16,\"hasPassword\":false,"
    "\"region\":\"AU\",\"modInfo\":{\"Mods\":["
    "{\"Name\":\"Req\",\"Version\":\"1.0.0\",\"RequiredOnClient\":true},"
    "{\"Name\":\"Opt\",\"Version\":\"2.0.0\",\"RequiredOnClient\":false}]}}";

int main(int argc, char** argv) {
    std::string s;
    // Escapes, including \u and a surrogate pair, decode to UTF-8.
    assert(DecodeJsonString("\"a\\\"b\\\\c\\/d\\n\"", s) && s == "a\"b\\c/d\n");
    assert(DecodeJsonString("\"\\u00e9\"", s) && s == "\xc3\xa9");
    assert(DecodeJsonString("\"\\ud83d\\ude00\"", s) && s == "\xf0\x9f\x98\x80");
    assert(!DecodeJsonString("\"unterminated", s));
    assert(!DecodeJsonString(nullptr, s));
    // Long strings are accepted, not rejected like JsonExtractString does.
    std::string longText = "\"" + std::string(5000, 'x') + "\"";
    assert(DecodeJsonString(longText.c_str(), s) && s.size() == 5000);

    // A complete server, with only client-required mods kept.
    RemoteServer server;
    assert(ParseServer(kServer, server));
    assert(server.id == "abc" && server.map == "mp_glitch" && server.playlist == "tdm");
    assert(server.playerCount == 3 && server.maxPlayers == 16 && !server.requiresPassword);
    assert(server.region == "AU");
    assert(server.requiredMods.size() == 1 && server.requiredMods[0].name == "Req");

    // PC's fixed array sizes truncate; description does not.
    std::string longName = std::string("{\"id\":\"i\",\"name\":\"") + std::string(100, 'n') +
        "\",\"description\":\"" + std::string(300, 'd') + "\",\"map\":\"m\",\"playlist\":\"" +
        std::string(40, 'p') + "\",\"playerCount\":0,\"maxPlayers\":1,\"hasPassword\":true,"
        "\"modInfo\":{\"Mods\":[]}}";
    assert(ParseServer(longName.c_str(), server));
    assert(server.name.size() == 63 && server.playlist.size() == 15);
    assert(server.description.size() == 300 && server.requiresPassword);
    assert(server.region.empty());  // absent region is allowed, as on PC

    // Missing required members skip the server.
    assert(!ParseServer("{\"id\":\"x\"}", server));
    assert(!ParseServer("{\"id\":\"x\",\"name\":\"n\",\"description\":\"d\",\"map\":\"m\","
        "\"playlist\":\"p\",\"playerCount\":\"3\",\"maxPlayers\":1,\"hasPassword\":false,"
        "\"modInfo\":{\"Mods\":[]}}", server));  // playerCount must be a number

    // Whole-list results.
    std::vector<RemoteServer> list;
    std::size_t skipped = 0;
    std::string two = std::string("[") + kServer + ",{\"id\":\"bad\"}," + kServer + "]";
    assert(ParseServerList(two.c_str(), list, skipped) == ServerListResult::Ok);
    assert(list.size() == 2 && skipped == 1);
    assert(ParseServerList("[]", list, skipped) == ServerListResult::Ok && list.empty());
    assert(ParseServerList("{\"error\":{\"msg\":\"x\"}}", list, skipped) == ServerListResult::ErrorResponse);
    assert(ParseServerList("\"nope\"", list, skipped) == ServerListResult::NotArray);
    std::string cut = std::string("[") + kServer + ",{\"id\":\"trunc";
    assert(ParseServerList(cut.c_str(), list, skipped) == ServerListResult::Truncated);

    // Merge updates by id, appends new ones, and sorts by player count.
    std::vector<RemoteServer> into(2);
    into[0].id = "a"; into[0].playerCount = 1;
    into[1].id = "b"; into[1].playerCount = 5;
    std::vector<RemoteServer> incoming(2);
    incoming[0].id = "a"; incoming[0].playerCount = 9;
    incoming[1].id = "c"; incoming[1].playerCount = 2;
    MergeServerList(into, std::move(incoming));
    assert(into.size() == 3 && into[0].id == "a" && into[0].playerCount == 9);
    assert(into[1].id == "b" && into[2].id == "c");

    // Joining.
    assert(PercentEncode("a b&c=d/é") == "a%20b%26c%3Dd%2F%C3%A9");
    assert(PercentEncode("Az09-_.~") == "Az09-_.~");
    assert(IsDottedIpv4("203.0.113.7") && IsDottedIpv4("0.0.0.0"));
    assert(!IsDottedIpv4("203.0.113") && !IsDottedIpv4("203.0.113.256") && !IsDottedIpv4("1.2.3.4.5"));
    assert(!IsDottedIpv4("1.2.3.4; quit") && !IsDottedIpv4("1..2.3") && !IsDottedIpv4(""));
    assert(IsAuthToken("abcDEF0123-x") && !IsAuthToken("") && !IsAuthToken("a b"));
    assert(!IsAuthToken(std::string(32, 'a')));

    ServerAuthResponse auth;
    assert(ParseServerAuthResponse(
        "{\"success\":true,\"ip\":\"203.0.113.7\",\"port\":37015,\"authToken\":\"tok123\"}", auth));
    assert(auth.success && auth.ip == "203.0.113.7" && auth.port == 37015 && auth.authToken == "tok123");
    // Error bodies: msg, then enum, then PC's fallback.
    assert(!ParseServerAuthResponse("{\"success\":false,\"error\":{\"enum\":\"UNAUTHORIZED_PWD\","
        "\"msg\":\"Wrong password\"}}", auth) && auth.failureReason == "Wrong password");
    assert(!ParseServerAuthResponse("{\"success\":false,\"error\":{\"enum\":\"PLAYER_NOT_FOUND\"}}", auth) &&
        auth.failureReason == "PLAYER_NOT_FOUND");
    assert(!ParseServerAuthResponse("{\"success\":false,\"error\":{}}", auth) &&
        auth.failureReason == "No error message provided");
    assert(!ParseServerAuthResponse("not json", auth) && auth.failureReason == "Authentication Failed");
    assert(!ParseServerAuthResponse("{\"success\":true,\"ip\":\"1.2.3.4\"}", auth));
    // Hostile or malformed values never reach the connect command.
    assert(!ParseServerAuthResponse("{\"success\":true,\"ip\":\"1.2.3.4; quit\",\"port\":1,"
        "\"authToken\":\"t\"}", auth) && auth.failureReason == "Master server returned an invalid server address");
    assert(!ParseServerAuthResponse("{\"success\":true,\"ip\":\"1.2.3.4\",\"port\":70000,"
        "\"authToken\":\"t\"}", auth));
    assert(!ParseServerAuthResponse("{\"success\":true,\"ip\":\"1.2.3.4\",\"port\":1,"
        "\"authToken\":\"a;b\"}", auth) && auth.failureReason == "Master server returned an invalid auth token");
    // PC's 31-character token limit.
    std::string longToken = "{\"success\":true,\"ip\":\"1.2.3.4\",\"port\":1,\"authToken\":\"" +
        std::string(40, 'k') + "\"}";
    assert(ParseServerAuthResponse(longToken.c_str(), auth) && auth.authToken.size() == 31);

    if (argc > 1) {
        std::ifstream file(argv[1], std::ios::binary);
        std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        assert(!text.empty());
        // Independent count: top-level objects in the root array.
        std::size_t objects = 0;
        int depth = 0;
        bool inString = false;
        for (std::size_t i = 0; i < text.size(); ++i) {
            const char c = text[i];
            if (inString) {
                if (c == '\\') ++i;
                else if (c == '"') inString = false;
                continue;
            }
            if (c == '"') inString = true;
            else if (c == '{' || c == '[') { if (c == '{' && depth == 1) ++objects; ++depth; }
            else if (c == '}' || c == ']') --depth;
        }
        assert(ParseServerList(text.c_str(), list, skipped) == ServerListResult::Ok);
        std::printf("live: %zu objects, %zu parsed, %zu skipped\n", objects, list.size(), skipped);
        assert(list.size() + skipped == objects);
        for (const auto& srv : list) assert(!srv.id.empty() && !srv.map.empty());
        std::size_t withMods = 0;
        for (const auto& srv : list) withMods += srv.requiredMods.empty() ? 0 : 1;
        std::printf("live: %zu servers require client mods; first: %s (%d/%d) %s\n", withMods,
            list.empty() ? "-" : list[0].name.c_str(), list.empty() ? 0 : list[0].playerCount,
            list.empty() ? 0 : list[0].maxPlayers, list.empty() ? "" : list[0].map.c_str());
    }
    std::puts("server_list tests passed");
    return 0;
}
