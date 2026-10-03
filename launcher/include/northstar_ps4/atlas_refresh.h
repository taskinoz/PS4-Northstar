#pragma once
#include "json_text.h"
#include <cstddef>
#include <string>

// Atlas token refresh through the PC token helper (scripts/Start-AtlasTokenHelper.ps1).
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

} // namespace northstar::ps4::atlas
