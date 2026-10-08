#pragma once
#include "json_text.h"
#include <cstddef>
#include <string>

// Atlas token refresh through the PC token helper (token-helper/, NorthstarPS4TokenHelper.exe).
//
// Atlas mints a player token only from an EA authorization code
// (/client/origin_auth), which a PS4 cannot obtain. The helper runs on a PC
// with the EA app signed in, gets a code over the EA app's local SDK
// connection, exchanges it with Atlas and serves the new token to this
// runtime. atlas_identity.json then carries two extra members:
//
//   "refreshUrl": "http://<pc>:<port>/atlas/token"
//   "refreshKey": "<32 hex>"   (sent as X-NorthstarPS4-Key; pairs this
//                               console with that helper)
//
// The helper answers {"uid":"<digits>","playerToken":"<32 hex>"}.
//
// Pairing goes the other way once: the runtime listens on kSignInPort and the
// helper POSTs the identity (all four members, plus "code") to kSignInPath.
// The menu shows this console's address and a 4-digit code to type into the
// helper. A push is accepted from this machine (shadPS4 on the helper's PC),
// with the code, or with the key of the helper already paired.
#include <cstdint>
#include <cstdio>
namespace northstar::ps4::atlas {

inline bool IsHex32(const std::string& text) {
    if (text.size() != 32) return false;
    for (const char c : text)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    return true;
}

inline bool IsDigits(const std::string& text) {
    if (text.empty() || text.size() > 20) return false;
    for (const char c : text)
        if (c < '0' || c > '9') return false;
    return true;
}

// http(s)://host[:port]/path with no characters that would need escaping in
// JSON or that a URL may not hold unescaped.
inline bool IsRefreshUrl(const std::string& url) {
    const bool http = url.rfind("http://", 0) == 0, https = url.rfind("https://", 0) == 0;
    if (!http && !https) return false;
    const std::size_t hostStart = http ? 7 : 8;
    if (url.size() <= hostStart || url.size() > 256) return false;
    for (const char c : url)
        if (c <= ' ' || c > '~' || c == '"' || c == '\\' || c == '<' || c == '>') return false;
    return url[hostStart] != '/';
}

struct IdentityFields {
    std::string uid, token, refreshUrl, refreshKey;
    std::string code;  // pairing pushes only
};

struct IdentityVisitor {
    int depth = 0;
    std::string pending;
    IdentityFields fields;
    bool OnObjectBegin() { ++depth; return true; }
    bool OnObjectEnd() { --depth; pending.clear(); return true; }
    bool OnArrayBegin() { ++depth; return true; }
    bool OnArrayEnd() { --depth; return true; }
    bool OnKey(const char* key, std::size_t size) { pending.assign(key, size); return true; }
    bool OnString(const char* text, std::size_t size) {
        if (depth == 1) {
            if (pending == "uid") fields.uid.assign(text, size);
            else if (pending == "playerToken") fields.token.assign(text, size);
            else if (pending == "refreshUrl") fields.refreshUrl.assign(text, size);
            else if (pending == "refreshKey") fields.refreshKey.assign(text, size);
            else if (pending == "code") fields.code.assign(text, size);
        }
        pending.clear();
        return true;
    }
    bool OnBool(bool) { pending.clear(); return true; }
    bool OnNull() { pending.clear(); return true; }
    bool OnInteger(int) { pending.clear(); return true; }
    bool OnFloat(float) { pending.clear(); return true; }
};

// The identity file, or the helper's reply (same member names).
inline bool ParseIdentity(const char* json, IdentityFields& fields) {
    if (!json) return false;
    IdentityVisitor visitor;
    if (!mods::JsonParse(json, visitor).ok) return false;
    fields = visitor.fields;
    return true;
}

// A helper reply is usable only for the same account, with a well-formed
// token that differs from the one that was just refused.
inline bool AcceptRefreshedToken(const IdentityFields& reply, const std::string& uid, const std::string& oldToken,
    std::string& reason) {
    if (!IsDigits(reply.uid) || !IsHex32(reply.token)) {
        reason = "the token helper sent an unreadable reply";
        return false;
    }
    if (reply.uid != uid) {
        reason = "the token helper is signed in to a different EA account";
        return false;
    }
    if (reply.token == oldToken) {
        reason = "the token helper returned the token that was just refused";
        return false;
    }
    return true;
}

// atlas_identity.json as the helper and this runtime both write it. Every
// value is validated first (digits, hex, a plain URL), so nothing here needs
// JSON escaping.
inline std::string BuildIdentityJson(const IdentityFields& fields) {
    std::string json = "{\n  \"uid\": \"" + fields.uid + "\",\n  \"playerToken\": \"" + fields.token + "\"";
    if (IsRefreshUrl(fields.refreshUrl) && IsHex32(fields.refreshKey)) {
        json += ",\n  \"refreshUrl\": \"" + fields.refreshUrl + "\"";
        json += ",\n  \"refreshKey\": \"" + fields.refreshKey + "\"";
    }
    return json + "\n}\n";
}

constexpr int kSignInPort = 37012;
constexpr const char* kSignInPath = "/northstar/signin";
constexpr const char* kHelloPath = "/northstar/hello";
constexpr int kMaxWrongCodes = 5;
constexpr std::size_t kMaxSignInRequest = 4096;

inline std::string FormatSignInCode(std::uint32_t random) {
    char code[8];
    std::snprintf(code, sizeof(code), "%04u", static_cast<unsigned>(random % 10000));
    return code;
}

// The request line and Content-Length of an HTTP/1.x request head (the text
// before the blank line). False for anything malformed.
struct RequestHead {
    std::string method, path;
    std::size_t contentLength = 0;
    std::string key;  // X-NorthstarPS4-Key: the helper asking whether it is paired
};

// Whether `offered` is this console's pairing key, compared in constant time.
// An empty key on either side never matches.
inline bool KeyMatches(const std::string& offered, const std::string& paired) {
    if (offered.empty() || paired.empty() || offered.size() != paired.size()) return false;
    unsigned char difference = 0;
    for (std::size_t i = 0; i < paired.size(); ++i)
        difference |= static_cast<unsigned char>(offered[i] ^ paired[i]);
    return difference == 0;
}

inline bool ParseRequestHead(const std::string& head, RequestHead& out) {
    out = RequestHead{};
    const std::size_t lineEnd = head.find("\r\n");
    const std::string line = head.substr(0, lineEnd);
    const std::size_t space1 = line.find(' ');
    const std::size_t space2 = space1 == std::string::npos ? std::string::npos : line.find(' ', space1 + 1);
    if (space2 == std::string::npos || line.compare(space2 + 1, 7, "HTTP/1.") != 0) return false;
    out.method = line.substr(0, space1);
    out.path = line.substr(space1 + 1, space2 - space1 - 1);
    if (out.method.empty() || out.path.empty() || out.path[0] != '/') return false;
    std::size_t at = lineEnd;
    while (at != std::string::npos && at < head.size()) {
        const std::size_t start = at + 2;
        const std::size_t end = head.find("\r\n", start);
        const std::string header = head.substr(start, end == std::string::npos ? std::string::npos : end - start);
        const std::size_t colon = header.find(':');
        if (colon != std::string::npos) {
            std::string name = header.substr(0, colon);
            for (char& c : name) c = static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c);
            if (name == "content-length") {
                std::size_t i = colon + 1;
                while (i < header.size() && header[i] == ' ') ++i;
                std::size_t value = 0;
                bool digits = false;
                for (; i < header.size() && header[i] >= '0' && header[i] <= '9'; ++i) {
                    value = value * 10 + static_cast<std::size_t>(header[i] - '0');
                    if (value > kMaxSignInRequest) return false;
                    digits = true;
                }
                if (!digits) return false;
                out.contentLength = value;
            } else if (name == "x-northstarps4-key") {
                std::size_t i = colon + 1;
                while (i < header.size() && header[i] == ' ') ++i;
                std::size_t last = header.size();
                while (last > i && header[last - 1] == ' ') --last;
                out.key = header.substr(i, last - i);
            }
        }
        at = end;
    }
    return true;
}

// Whether a pairing push may replace this console's identity. Empty when it
// may; otherwise the reason, which is sent back to the helper. `wrongCodes`
// counts refused codes; after kMaxWrongCodes, only this machine and the
// paired helper can sign in until the game restarts.
inline std::string CheckSignInPush(const IdentityFields& push, bool fromThisMachine, const std::string& code,
    const std::string& pairedKey, int& wrongCodes) {
    if (!IsDigits(push.uid) || !IsHex32(push.token) || !IsRefreshUrl(push.refreshUrl) || !IsHex32(push.refreshKey))
        return "the sign-in is incomplete or malformed";
    if (fromThisMachine) return "";
    if (KeyMatches(push.refreshKey, pairedKey)) return "";
    if (wrongCodes >= kMaxWrongCodes) return "too many wrong codes; restart the game to get a new one";
    if (code.empty() || push.code != code) {
        ++wrongCodes;
        return "wrong code; type the code shown on the PS4";
    }
    return "";
}

} // namespace northstar::ps4::atlas
