#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <string>

// Text half of Northstar's DecodeJSON/EncodeJSON natives. PC Northstar hands
// this work to RapidJSON (scripts/scriptjson.cpp); the PS4 runtime has no
// RapidJSON, so the parse and the escaping live here, away from any engine
// state, and the Squirrel value building stays in the runtime module.
namespace northstar::ps4::mods {
constexpr int kJsonMaxDepth = 32;

struct JsonFailure {
    const char* at;
    const char* message;
};

// Records where parsing stopped so the script-side message can name the
// offset, exactly as PC reports RapidJSON errors.
inline const char* JsonFail(JsonFailure* failure, const char* at, const char* message) noexcept {
    failure->at = at;
    failure->message = message;
    return nullptr;
}

struct JsonParseResult {
    bool ok;
    std::size_t offset;   // byte offset of the failure, as PC reports
    const char* message;
};

inline const char* JsonTextSkipWs(const char* p) noexcept {
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') ++p;
    return p;
}

inline void JsonAppendUtf8(std::string& out, std::uint32_t code) {
    if (code < 0x80) {
        out.push_back(static_cast<char>(code));
    } else if (code < 0x800) {
        out.push_back(static_cast<char>(0xc0 | (code >> 6)));
        out.push_back(static_cast<char>(0x80 | (code & 0x3f)));
    } else if (code < 0x10000) {
        out.push_back(static_cast<char>(0xe0 | (code >> 12)));
        out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | (code & 0x3f)));
    } else {
        out.push_back(static_cast<char>(0xf0 | (code >> 18)));
        out.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | (code & 0x3f)));
    }
}

inline bool JsonReadHex4(const char* p, std::uint32_t& out) noexcept {
    out = 0;
    for (int i = 0; i < 4; ++i) {
        const char c = p[i];
        std::uint32_t digit;
        if (c >= '0' && c <= '9') digit = static_cast<std::uint32_t>(c - '0');
        else if (c >= 'a' && c <= 'f') digit = static_cast<std::uint32_t>(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') digit = static_cast<std::uint32_t>(c - 'A' + 10);
        else return false;
        out = (out << 4) | digit;
    }
    return true;
}

// Reads one JSON string literal starting at the opening quote.
inline const char* JsonReadString(const char* p, std::string& out) {
    if (*p != '"') return nullptr;
    out.clear();
    ++p;
    for (;;) {
        const unsigned char c = static_cast<unsigned char>(*p);
        if (c == '"') return p + 1;
        if (c == '\0') return nullptr;
        if (c < 0x20) return nullptr;
        if (c != '\\') { out.push_back(*p++); continue; }
        ++p;
        switch (*p) {
        case '"': out.push_back('"'); ++p; break;
        case '\\': out.push_back('\\'); ++p; break;
        case '/': out.push_back('/'); ++p; break;
        case 'b': out.push_back('\b'); ++p; break;
        case 'f': out.push_back('\f'); ++p; break;
        case 'n': out.push_back('\n'); ++p; break;
        case 'r': out.push_back('\r'); ++p; break;
        case 't': out.push_back('\t'); ++p; break;
        case 'u': {
            std::uint32_t code = 0;
            if (!JsonReadHex4(p + 1, code)) return nullptr;
            p += 5;
            if (code >= 0xd800 && code <= 0xdbff && p[0] == '\\' && p[1] == 'u') {
                std::uint32_t low = 0;
                if (!JsonReadHex4(p + 2, low)) return nullptr;
                if (low >= 0xdc00 && low <= 0xdfff) {
                    code = 0x10000 + ((code - 0xd800) << 10) + (low - 0xdc00);
                    p += 6;
                }
            }
            JsonAppendUtf8(out, code);
            break;
        }
        default: return nullptr;
        }
    }
}

// Squirrel has no NUL-safe strings, so a decoded NUL would silently truncate
// the value; PC's writers reject NUL for the same reason.
inline bool JsonTextUsable(const std::string& text) noexcept {
    return text.find('\0') == std::string::npos;
}

// Visitor calls: OnObjectBegin/OnObjectEnd, OnArrayBegin/OnArrayEnd, OnKey,
// OnString, OnInteger, OnFloat, OnBool, OnNull. Any false return aborts.
template <typename Visitor>
const char* JsonParseValue(const char* p, Visitor& visitor, int depth, JsonFailure* error) {
    if (depth > kJsonMaxDepth) { return JsonFail(error, p, "document is nested too deeply"); }
    p = JsonTextSkipWs(p);
    switch (*p) {
    case '{': {
        if (!visitor.OnObjectBegin()) { return JsonFail(error, p, "object could not be created"); }
        p = JsonTextSkipWs(p + 1);
        if (*p == '}') return visitor.OnObjectEnd() ? p + 1 : JsonFail(error, p, "object could not be stored");
        for (;;) {
            std::string key;
            const char* name = JsonTextSkipWs(p);
            p = JsonReadString(name, key);
            if (!p) { return JsonFail(error, name, "expected a string member name"); }
            if (!JsonTextUsable(key)) { return JsonFail(error, name, "member names may not contain NUL"); }
            p = JsonTextSkipWs(p);
            if (*p != ':') { return JsonFail(error, p, "expected ':' after a member name"); }
            if (!visitor.OnKey(key.c_str(), key.size())) { return JsonFail(error, p, "member name could not be stored"); }
            p = JsonParseValue(p + 1, visitor, depth + 1, error);
            if (!p) return nullptr;
            p = JsonTextSkipWs(p);
            if (*p == ',') { ++p; continue; }
            if (*p == '}') return visitor.OnObjectEnd() ? p + 1 : JsonFail(error, p, "object could not be stored");
            return JsonFail(error, p, "expected ',' or '}'");
        }
    }
    case '[': {
        if (!visitor.OnArrayBegin()) { return JsonFail(error, p, "array could not be created"); }
        p = JsonTextSkipWs(p + 1);
        if (*p == ']') return visitor.OnArrayEnd() ? p + 1 : JsonFail(error, p, "array could not be stored");
        for (;;) {
            p = JsonParseValue(p, visitor, depth + 1, error);
            if (!p) return nullptr;
            p = JsonTextSkipWs(p);
            if (*p == ',') { ++p; continue; }
            if (*p == ']') return visitor.OnArrayEnd() ? p + 1 : JsonFail(error, p, "array could not be stored");
            return JsonFail(error, p, "expected ',' or ']'");
        }
    }
    case '"': {
        std::string text;
        const char* end = JsonReadString(p, text);
        if (!end) { return JsonFail(error, p, "invalid string"); }
        if (!JsonTextUsable(text)) { return JsonFail(error, p, "strings may not contain NUL"); }
        if (!visitor.OnString(text.c_str(), text.size())) { return JsonFail(error, p, "string could not be stored"); }
        return end;
    }
    case 't':
        if (std::strncmp(p, "true", 4)) break;
        return visitor.OnBool(true) ? p + 4 : JsonFail(error, p, "value could not be stored");
    case 'f':
        if (std::strncmp(p, "false", 5)) break;
        return visitor.OnBool(false) ? p + 5 : JsonFail(error, p, "value could not be stored");
    case 'n':
        if (std::strncmp(p, "null", 4)) break;
        return visitor.OnNull() ? p + 4 : JsonFail(error, p, "value could not be stored");
    default: break;
    }
    // Numbers. PC keeps whole numbers as Squirrel integers and everything
    // else as floats, so the same split is applied here.
    const char* start = p;
    if (*p == '-') ++p;
    if (*p < '0' || *p > '9') { return JsonFail(error, p, "expected a value"); }
    const char* digits = p;
    while (*p >= '0' && *p <= '9') ++p;
    // JSON forbids leading zeros; RapidJSON rejects them on PC.
    if (*digits == '0' && p - digits > 1) return JsonFail(error, digits, "numbers may not have leading zeros");
    bool fractional = false;
    if (*p == '.') {
        fractional = true;
        ++p;
        if (*p < '0' || *p > '9') { return JsonFail(error, p, "expected digits after '.'"); }
        while (*p >= '0' && *p <= '9') ++p;
    }
    if (*p == 'e' || *p == 'E') {
        fractional = true;
        ++p;
        if (*p == '+' || *p == '-') ++p;
        if (*p < '0' || *p > '9') { return JsonFail(error, p, "expected digits in the exponent"); }
        while (*p >= '0' && *p <= '9') ++p;
    }
    const std::string number(start, static_cast<std::size_t>(p - start));
    if (!fractional) {
        errno = 0;
        const long long value = std::strtoll(number.c_str(), nullptr, 10);
        // Squirrel integers are 32-bit; anything wider keeps its magnitude as
        // a float rather than silently wrapping.
        if (errno == 0 && value >= -2147483648LL && value <= 2147483647LL)
            return visitor.OnInteger(static_cast<int>(value)) ? p : JsonFail(error, p, "value could not be stored");
    }
    return visitor.OnFloat(static_cast<float>(std::strtod(number.c_str(), nullptr)))
        ? p : JsonFail(error, p, "value could not be stored");
}

template <typename Visitor>
JsonParseResult JsonParse(const char* text, Visitor& visitor) {
    if (!text) return {false, 0, "no document"};
    JsonFailure error{text, nullptr};
    const char* end = JsonParseValue(text, visitor, 0, &error);
    if (!end) return {false, static_cast<std::size_t>(error.at - text),
        error.message ? error.message : "invalid document"};
    end = JsonTextSkipWs(end);
    if (*end) return {false, static_cast<std::size_t>(end - text), "trailing content after the document"};
    return {true, static_cast<std::size_t>(end - text), nullptr};
}

inline void JsonEscapeInto(std::string& out, const char* text, std::size_t length) {
    out.push_back('"');
    for (std::size_t i = 0; i < length; ++i) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) {
                char escape[7];
                std::snprintf(escape, sizeof(escape), "\\u%04x", c);
                out += escape;
            } else {
                out.push_back(static_cast<char>(c));
            }
        }
    }
    out.push_back('"');
}

inline void JsonAppendFloat(std::string& out, float value) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.9g", static_cast<double>(value));
    // JSON has no Infinity or NaN; PC's writer emits nothing usable for them
    // either, so they become null rather than invalid output.
    for (const char* p = buffer; *p; ++p)
        if (*p == 'i' || *p == 'n' || *p == 'I' || *p == 'N') { out += "null"; return; }
    out += buffer;
}
} // namespace northstar::ps4::mods
