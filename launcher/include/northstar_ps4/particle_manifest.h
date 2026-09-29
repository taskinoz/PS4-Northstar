#pragma once

#include <cctype>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace northstar::ps4::mods {

inline const char* SkipParticleSpaceAndComments(const char* p) noexcept {
    for (;;) {
        while (*p && std::isspace(static_cast<unsigned char>(*p))) ++p;
        if (p[0] == '/' && p[1] == '/') {
            p += 2;
            while (*p && *p != '\n') ++p;
            continue;
        }
        if (p[0] == '/' && p[1] == '*') {
            p += 2;
            while (*p && !(p[0] == '*' && p[1] == '/')) ++p;
            if (*p) p += 2;
            continue;
        }
        return p;
    }
}

// Return a Source particles_manifest root's contents without interpreting its
// entries. Keeping the text intact preserves duplicate keys, comments and
// engine-specific syntax while several manifests are combined safely.
inline bool ExtractParticleManifestBody(
    const std::string& text, std::string& body, std::string& error) {
    body.clear();
    error.clear();
    const char* p = text.c_str();
    if (text.size() >= 3 && static_cast<unsigned char>(p[0]) == 0xef &&
        static_cast<unsigned char>(p[1]) == 0xbb && static_cast<unsigned char>(p[2]) == 0xbf)
        p += 3;

    // PC accepts #base lines before the root. They are not followed here: the
    // runtime reads the retail manifest separately and combines enabled mods.
    for (;;) {
        p = SkipParticleSpaceAndComments(p);
        if (std::strncmp(p, "#base", 5) != 0 ||
            (p[5] && !std::isspace(static_cast<unsigned char>(p[5])))) break;
        while (*p && *p != '\n') ++p;
    }
    p = SkipParticleSpaceAndComments(p);
    const char* const root = "particles_manifest";
    if (*p == '"') {
        ++p;
        const char* start = p;
        while (*p && *p != '"') ++p;
        if (std::string(start, p) != root || *p != '"') {
            error = "expected particles_manifest root";
            return false;
        }
        ++p;
    } else {
        const char* start = p;
        while (*p && !std::isspace(static_cast<unsigned char>(*p)) && *p != '{') ++p;
        if (std::string(start, p) != root) {
            error = "expected particles_manifest root";
            return false;
        }
    }
    p = SkipParticleSpaceAndComments(p);
    if (*p != '{') {
        error = "expected opening brace";
        return false;
    }
    const char* const bodyStart = ++p;
    int depth = 1;
    bool quoted = false;
    bool escaped = false;
    for (; *p; ++p) {
        if (quoted) {
            if (escaped) escaped = false;
            else if (*p == '\\') escaped = true;
            else if (*p == '"') quoted = false;
            continue;
        }
        if (*p == '"') { quoted = true; continue; }
        if (p[0] == '/' && p[1] == '/') {
            while (*p && *p != '\n') ++p;
            if (!*p) break;
            continue;
        }
        if (p[0] == '/' && p[1] == '*') {
            p += 2;
            while (*p && !(p[0] == '*' && p[1] == '/')) ++p;
            if (!*p) break;
            ++p;
            continue;
        }
        if (*p == '{') ++depth;
        else if (*p == '}' && --depth == 0) {
            body.assign(bodyStart, p);
            const char* tail = SkipParticleSpaceAndComments(p + 1);
            if (*tail) {
                error = "unexpected text after root block";
                body.clear();
                return false;
            }
            return true;
        }
    }
    error = quoted ? "unterminated string" : "unterminated root block";
    return false;
}

inline std::string BuildParticleManifest(const std::string& originalBody,
    const std::vector<std::pair<std::string, std::string>>& modBodies) {
    std::string output = "particles_manifest\n{\n";
    output += originalBody;
    if (!output.empty() && output.back() != '\n') output += '\n';
    for (const auto& entry : modBodies) {
        output += "\t// [" + entry.first + "]\n";
        output += entry.second;
        if (!output.empty() && output.back() != '\n') output += '\n';
    }
    output += "}\n";
    return output;
}

}  // namespace northstar::ps4::mods
