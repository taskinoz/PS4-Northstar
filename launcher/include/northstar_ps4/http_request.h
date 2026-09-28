#pragma once

// The parts of PC Northstar's script HTTP requests
// (primedev/scripts/scripthttprequesthandler.*) that do not touch the network,
// so they can be host-tested (tests/http_request.cpp): method names and
// options, the query string curl builds, the destination host, and the
// private-network check that -allowlocalhttp lifts.

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace northstar::ps4::http {

// HttpRequestMethod, matching sh_northstar_http_requests.gnut.
enum Method { kGet = 0, kPost = 1, kHead = 2, kPut = 3, kDelete = 4, kPatch = 5, kOptions = 6 };

inline const char* MethodName(int method) {
    switch (method) {
    case kGet: return "GET";
    case kPost: return "POST";
    case kHead: return "HEAD";
    case kPut: return "PUT";
    case kDelete: return "DELETE";
    case kPatch: return "PATCH";
    case kOptions: return "OPTIONS";
    default: return "INVALID";
    }
}

// Methods whose body is sent the way curl sends a POST.
inline bool UsesPostOptions(int method) {
    return method == kPost || method == kPut || method == kDelete || method == kPatch;
}

inline bool CanHaveQueryParameters(int method) { return method == kGet || UsesPostOptions(method); }

// curl_easy_escape: everything but ALPHA / DIGIT / "-" / "." / "_" / "~" as
// %XX with upper-case hex.
inline std::string UrlEscape(const std::string& text) {
    static const char kHex[] = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : text) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '.' ||
            c == '_' || c == '~') {
            out += static_cast<char>(c);
        } else {
            out += '%';
            out += kHex[c >> 4];
            out += kHex[c & 15];
        }
    }
    return out;
}

// CURLU_DEFAULT_SCHEME: a URL without a scheme is https.
inline std::string WithDefaultScheme(const std::string& url) {
    const std::size_t colon = url.find("://");
    if (colon == std::string::npos) return "https://" + url;
    return url;
}

inline std::string Lower(std::string text) {
    for (auto& c : text)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return text;
}

struct UrlParts {
    std::string scheme;  // lower case
    std::string host;    // without brackets for an IPv6 literal
    int port = 0;        // the scheme's default when the URL has none
    bool ipv6Literal = false;
};

// Scheme, host and port of an absolute URL (after WithDefaultScheme). False for
// anything curl's URL parser would refuse outright: no host, a bad port.
inline bool ParseUrl(const std::string& url, UrlParts& out) {
    const std::size_t colon = url.find("://");
    if (colon == std::string::npos || colon == 0) return false;
    out.scheme = Lower(url.substr(0, colon));
    std::size_t start = colon + 3;
    std::size_t end = url.find_first_of("/?#", start);
    std::string authority = url.substr(start, end == std::string::npos ? std::string::npos : end - start);
    const std::size_t at = authority.rfind('@');
    if (at != std::string::npos) authority = authority.substr(at + 1);
    std::string portText;
    if (!authority.empty() && authority[0] == '[') {
        const std::size_t close = authority.find(']');
        if (close == std::string::npos) return false;
        out.host = authority.substr(1, close - 1);
        out.ipv6Literal = true;
        if (close + 1 < authority.size()) {
            if (authority[close + 1] != ':') return false;
            portText = authority.substr(close + 2);
        }
    } else {
        const std::size_t portColon = authority.rfind(':');
        out.host = authority.substr(0, portColon);
        if (portColon != std::string::npos) portText = authority.substr(portColon + 1);
    }
    if (out.host.empty()) return false;
    out.port = out.scheme == "https" ? 443 : out.scheme == "http" ? 80 : 0;
    if (!portText.empty()) {
        int port = 0;
        for (char c : portText) {
            if (c < '0' || c > '9') return false;
            port = port * 10 + (c - '0');
            if (port > 65535) return false;
        }
        out.port = port;
    }
    return true;
}

// Whether the URL already has a non-empty query, so parameters are appended
// with '&' rather than starting one with '?'.
inline bool UrlHasQuery(const std::string& url) {
    const std::size_t hash = url.find('#');
    const std::size_t mark = url.find('?');
    if (mark == std::string::npos || (hash != std::string::npos && mark > hash)) return false;
    const std::size_t end = hash == std::string::npos ? url.size() : hash;
    return end > mark + 1;
}

using Parameters = std::vector<std::pair<std::string, std::vector<std::string>>>;

// PC appends the query parameters when the method takes them but is not
// POST-like, or when the body is empty (`a && !b || c`, as written there).
inline std::string BuildRequestUrl(const std::string& url, int method, bool bodyEmpty, const Parameters& query) {
    std::string out = url;
    if (!((CanHaveQueryParameters(method) && !UsesPostOptions(method)) || bodyEmpty)) return out;
    bool first = !UrlHasQuery(url);
    for (const auto& parameter : query) {
        const std::string key = UrlEscape(parameter.first);
        for (const auto& value : parameter.second) {
            out += first ? '?' : '&';
            first = false;
            out += key;
            out += '=';
            out += UrlEscape(value);
        }
    }
    return out;
}

// The address ranges PC refuses without -allowlocalhttp: private, loopback,
// link-local, shared, test, benchmark, relay, multicast and reserved space.
inline bool IsPrivateIPv4(std::uint8_t a, std::uint8_t b, std::uint8_t c, std::uint8_t d) {
    return a == 10 || (a == 172 && b >= 16 && b <= 31) || (a == 192 && b == 168) || (a == 192 && b == 0 && c == 0) ||
        (a == 192 && b == 0 && c == 2) || (a == 192 && b == 88 && c == 99) || (a == 198 && b >= 18 && b <= 19) ||
        (a == 198 && b == 51 && c == 100) || (a == 203 && b == 0 && c == 113) || (a == 169 && b == 254) || a == 127 ||
        a == 0 || (a == 100 && b >= 64 && b <= 127) || (a == 255 && b == 255 && c == 255 && d == 255) ||
        (a >= 224 && a <= 239) || (a == 233 && b == 252 && c == 0) || a >= 240;
}

// A dotted IPv4 literal, as the address bytes; false for anything else.
inline bool ParseIPv4(const std::string& text, std::uint8_t out[4]) {
    int part = 0;
    int value = -1;
    for (std::size_t i = 0; i <= text.size(); ++i) {
        const char c = i < text.size() ? text[i] : '.';
        if (c >= '0' && c <= '9') {
            value = (value < 0 ? 0 : value * 10) + (c - '0');
            if (value > 255) return false;
        } else if (c == '.') {
            if (value < 0 || part > 3) return false;
            out[part++] = static_cast<std::uint8_t>(value);
            value = -1;
        } else {
            return false;
        }
    }
    return part == 4;
}

} // namespace northstar::ps4::http
