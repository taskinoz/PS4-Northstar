#pragma once
// Parsing of Atlas's /client/servers response, matching NorthstarLauncher's
// MasterServerManager::RequestServerList (primedev/masterserver/masterserver.cpp).
// Pure and host-testable; the runtime's fetch, threading and Squirrel pushes
// live in launcher/src/runtime_server_list.inl.
#include "northstar_ps4/mod_catalog.h"
#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace northstar::ps4::mods {

struct RemoteMod { std::string name, version; };
struct RemoteServer {
    std::string id, name, description, map, playlist, region;
    int playerCount = 0, maxPlayers = 0;
    bool requiresPassword = false;
    std::vector<RemoteMod> requiredMods;
};

// Decodes a JSON string value to UTF-8, including \uXXXX and surrogate pairs.
// JsonExtractString neither decodes \u nor accepts a value longer than its
// buffer, and a long description would otherwise drop the whole server.
inline bool DecodeJsonString(const char* p, std::string& out) {
    out.clear();
    if (!p) return false;
    p = JsonSkipWs(p);
    if (*p != '"') return false;
    ++p;
    auto hex4 = [](const char* h, unsigned& value) {
        value = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = h[i];
            value <<= 4;
            if (c >= '0' && c <= '9') value |= unsigned(c - '0');
            else if (c >= 'a' && c <= 'f') value |= unsigned(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') value |= unsigned(c - 'A' + 10);
            else return false;
        }
        return true;
    };
    auto putUtf8 = [&out](unsigned cp) {
        if (cp < 0x80) {
            out += char(cp);
        } else if (cp < 0x800) {
            out += char(0xc0 | (cp >> 6));
            out += char(0x80 | (cp & 0x3f));
        } else if (cp < 0x10000) {
            out += char(0xe0 | (cp >> 12));
            out += char(0x80 | ((cp >> 6) & 0x3f));
            out += char(0x80 | (cp & 0x3f));
        } else {
            out += char(0xf0 | (cp >> 18));
            out += char(0x80 | ((cp >> 12) & 0x3f));
            out += char(0x80 | ((cp >> 6) & 0x3f));
            out += char(0x80 | (cp & 0x3f));
        }
    };
    while (*p && *p != '"') {
        if (*p != '\\') {
            out += *p++;
            continue;
        }
        const char e = p[1];
        if (!e) return false;
        p += 2;
        switch (e) {
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u': {
                unsigned cp = 0;
                if (!hex4(p, cp)) return false;
                p += 4;
                if (cp >= 0xd800 && cp <= 0xdbff && p[0] == '\\' && p[1] == 'u') {
                    unsigned low = 0;
                    if (hex4(p + 2, low) && low >= 0xdc00 && low <= 0xdfff) {
                        cp = 0x10000 + ((cp - 0xd800) << 10) + (low - 0xdc00);
                        p += 6;
                    }
                }
                putUtf8(cp);
                break;
            }
            default: out += e; break;  // \" \\ \/
        }
    }
    return *p == '"';
}

inline bool JsonIsNumber(const char* p) {
    if (!p) return false;
    p = JsonSkipWs(p);
    return *p == '-' || (*p >= '0' && *p <= '9');
}

inline bool JsonBool(const char* p, bool& value) {
    if (!p) return false;
    p = JsonSkipWs(p);
    if (std::strncmp(p, "true", 4) == 0) { value = true; return true; }
    if (std::strncmp(p, "false", 5) == 0) { value = false; return true; }
    return false;
}

// PC keeps these fields in fixed char arrays; strncpy semantics keep size - 1.
inline void ClipToPcArray(std::string& s, std::size_t pcArraySize) {
    if (s.size() > pcArraySize - 1) s.resize(pcArraySize - 1);
}

// Same acceptance rules as PC: string id, name, description, map and playlist,
// numeric playerCount and maxPlayers, boolean hasPassword and a modInfo.Mods
// array, or the server is skipped. Required mods are those with
// RequiredOnClient true. Fields are cut to the sizes of PC's RemoteServerInfo
// arrays; description is a std::string there and is unbounded here too.
inline bool ParseServer(const char* object, RemoteServer& server) {
    const char* players = JsonFindMember(object, "playerCount");
    const char* maxPlayers = JsonFindMember(object, "maxPlayers");
    const char* modInfo = JsonFindMember(object, "modInfo");
    const char* mods = modInfo ? JsonFindMember(modInfo, "Mods") : nullptr;
    if (!DecodeJsonString(JsonFindMember(object, "id"), server.id) ||
        !DecodeJsonString(JsonFindMember(object, "name"), server.name) ||
        !DecodeJsonString(JsonFindMember(object, "description"), server.description) ||
        !DecodeJsonString(JsonFindMember(object, "map"), server.map) ||
        !DecodeJsonString(JsonFindMember(object, "playlist"), server.playlist) ||
        !JsonIsNumber(players) || !JsonIsNumber(maxPlayers) ||
        !JsonBool(JsonFindMember(object, "hasPassword"), server.requiresPassword) ||
        !mods || *JsonSkipWs(mods) != '[')
        return false;
    if (!DecodeJsonString(JsonFindMember(object, "region"), server.region)) server.region.clear();
    ClipToPcArray(server.id, 33);
    ClipToPcArray(server.name, 64);
    ClipToPcArray(server.map, 32);
    ClipToPcArray(server.playlist, 16);
    ClipToPcArray(server.region, 32);
    server.playerCount = static_cast<int>(JsonExtractInteger(players));
    server.maxPlayers = static_cast<int>(JsonExtractInteger(maxPlayers));

    server.requiredMods.clear();
    const char* p = JsonSkipWs(JsonSkipWs(mods) + 1);
    while (*p && *p != ']') {
        bool required = false;
        RemoteMod mod;
        if (*p == '{' && JsonBool(JsonFindMember(p, "RequiredOnClient"), required) && required &&
            DecodeJsonString(JsonFindMember(p, "Name"), mod.name) &&
            DecodeJsonString(JsonFindMember(p, "Version"), mod.version))
            server.requiredMods.push_back(std::move(mod));
        p = JsonSkipWs(JsonSkipValue(p));
        if (*p != ',') break;
        p = JsonSkipWs(p + 1);
    }
    return true;
}

enum class ServerListResult { Ok, ErrorResponse, NotArray, Truncated };

inline const char* ServerListResultText(ServerListResult r) {
    switch (r) {
        case ServerListResult::Ok: return "ok";
        case ServerListResult::ErrorResponse: return "master server returned an error";
        case ServerListResult::NotArray: return "root is not an array";
        case ServerListResult::Truncated: return "array ended unexpectedly";
    }
    return "?";
}

// Parses the whole response. Malformed server objects are skipped and counted,
// as on PC; a broken array (a non-object element, or no closing bracket) is a
// failure, because it means the text was cut short.
inline ServerListResult ParseServerList(const char* text, std::vector<RemoteServer>& out,
    std::size_t& skipped) {
    out.clear();
    skipped = 0;
    const char* p = JsonSkipWs(text);
    if (*p == '{') return JsonFindMember(p, "error") ? ServerListResult::ErrorResponse
                                                     : ServerListResult::NotArray;
    if (*p != '[') return ServerListResult::NotArray;
    p = JsonSkipWs(p + 1);
    while (*p != ']') {
        if (*p != '{') return ServerListResult::Truncated;
        RemoteServer server;
        if (ParseServer(p, server)) out.push_back(std::move(server));
        else ++skipped;
        p = JsonSkipWs(JsonSkipValue(p));
        if (*p == ',') { p = JsonSkipWs(p + 1); continue; }
        if (*p != ']') return ServerListResult::Truncated;
    }
    return ServerListResult::Ok;
}

// PC updates entries it already has by id and appends new ones, then sorts by
// player count, descending. Servers that have gone away stay until the script
// calls NSClearRecievedServerList, which the browser does before each request.
inline void MergeServerList(std::vector<RemoteServer>& into, std::vector<RemoteServer>&& incoming) {
    for (auto& server : incoming) {
        auto existing = std::find_if(into.begin(), into.end(),
            [&](const RemoteServer& s) { return s.id == server.id; });
        if (existing != into.end()) *existing = std::move(server);
        else into.push_back(std::move(server));
    }
    std::stable_sort(into.begin(), into.end(),
        [](const RemoteServer& a, const RemoteServer& b) { return a.playerCount > b.playerCount; });
}

// ---------------------------------------------------------------------------
// Joining: POST <master>/client/auth_with_server, as PC's
// MasterServerManager::AuthenticateWithServer does.

// RFC 3986 unreserved characters pass; everything else is %XX. PC escapes only
// the password (curl_easy_escape); escaping every value is equivalent for the
// uid, token and server id, which are plain alphanumerics anyway.
inline std::string PercentEncode(const std::string& in) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : in) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~') {
            out += char(c);
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    return out;
}

// The values below end up in a `connect` console command and the serverFilter
// convar, so they are held to exactly the shapes Atlas produces. PC parses the
// ip with inet_addr and copies the token raw; a master server returning
// anything else here is malformed, and on this port it would otherwise be text
// spliced into a command line.
inline bool IsDottedIpv4(const std::string& ip) {
    int parts = 0, digits = 0, value = 0;
    for (char c : ip) {
        if (c >= '0' && c <= '9') {
            value = value * 10 + (c - '0');
            if (++digits > 3 || value > 255) return false;
        } else if (c == '.') {
            if (digits == 0 || ++parts > 3) return false;
            digits = value = 0;
        } else {
            return false;
        }
    }
    return parts == 3 && digits > 0;
}

inline bool IsAuthToken(const std::string& token) {
    if (token.empty() || token.size() > 31) return false;
    for (char c : token)
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-'))
            return false;
    return true;
}

struct ServerAuthResponse {
    bool success = false;
    std::string ip, authToken, failureReason;
    std::string errorEnum;  // Atlas's error.enum, when it sent one
    int port = 0;
};

// Atlas answers with {success:true, ip, port, authToken} or, with a non-2xx
// status, {success:false, error:{enum, msg}}. The body is parsed either way,
// because the reason is in it. The reason text follows PC: error.msg, then
// error.enum, then "No error message provided"; anything else malformed is
// PC's generic "Authentication Failed".
inline bool ParseServerAuthResponse(const char* text, ServerAuthResponse& out) {
    out = ServerAuthResponse{};
    out.failureReason = "Authentication Failed";
    const char* root = text ? JsonSkipWs(text) : nullptr;
    if (!root || *root != '{') return false;
    if (const char* error = JsonFindMember(root, "error")) {
        if (!DecodeJsonString(JsonFindMember(error, "enum"), out.errorEnum)) out.errorEnum.clear();
        std::string reason;
        if (DecodeJsonString(JsonFindMember(error, "msg"), reason) && !reason.empty())
            out.failureReason = reason;
        else if (DecodeJsonString(JsonFindMember(error, "enum"), reason) && !reason.empty())
            out.failureReason = reason;
        else
            out.failureReason = "No error message provided";
        return false;
    }
    bool success = false;
    if (!JsonBool(JsonFindMember(root, "success"), success) || !success) return false;
    const char* port = JsonFindMember(root, "port");
    if (!DecodeJsonString(JsonFindMember(root, "ip"), out.ip) || !JsonIsNumber(port) ||
        !DecodeJsonString(JsonFindMember(root, "authToken"), out.authToken))
        return false;
    const long long portValue = JsonExtractInteger(port);
    if (!IsDottedIpv4(out.ip) || portValue < 1 || portValue > 65535) {
        out.failureReason = "Master server returned an invalid server address";
        return false;
    }
    // PC keeps 31 characters (char authToken[32]); a longer one is cut the same way.
    ClipToPcArray(out.authToken, 32);
    if (!IsAuthToken(out.authToken)) {
        out.failureReason = "Master server returned an invalid auth token";
        return false;
    }
    out.port = static_cast<int>(portValue);
    out.success = true;
    out.failureReason.clear();
    return true;
}

} // namespace northstar::ps4::mods
