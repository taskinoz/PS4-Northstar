#pragma once
#include "mod_catalog.h"
#include "json_text.h"
#include <string>
namespace northstar::ps4::mods {
inline bool ModVpkStem(const char* filename, std::string& stem) {
    if (!filename || std::strchr(filename, '/') || std::strchr(filename, '\\') || std::strchr(filename, ':')) return false;
    const std::string name(filename), prefix("english"), suffix(".pak000_dir.vpk");
    if (name.size() <= prefix.size() + suffix.size() || name.compare(0, prefix.size(), prefix) ||
        name.compare(name.size() - suffix.size(), suffix.size(), suffix)) return false;
    std::string candidate = name.substr(prefix.size(), name.size() - prefix.size() - suffix.size());
    if (candidate.size() < 5 || candidate.compare(candidate.size() - 4, 4, ".bsp") || candidate.find("..") != std::string::npos) return false;
    stem.swap(candidate); return true;
}
// Read only the first top-level Preload member, as RapidJSON FindMember does.
struct VpkConfigVisitor {
    int depth = 0;
    bool seen = false, selected = false, preload = false;
    bool OnObjectBegin() { ++depth; selected = false; return true; }
    bool OnObjectEnd() { --depth; return true; }
    bool OnArrayBegin() { ++depth; selected = false; return true; }
    bool OnArrayEnd() { --depth; return true; }
    bool OnKey(const char* key, std::size_t size) {
        selected = depth == 1 && !seen && size == 7 && !std::memcmp(key, "Preload", 7);
        if (selected) seen = true;
        return true;
    }
    bool OnBool(bool value) { if (selected) preload = value; selected = false; return true; }
    bool OnNull() { selected = false; return true; }
    bool OnInteger(int) { return OnNull(); }
    bool OnFloat(float) { return OnNull(); }
    bool OnString(const char*, std::size_t) { return OnNull(); }
};
inline bool VpkPreload(const char* config) {
    if (!config) return true;
    // PC permits comments and trailing commas. Normalize those outside strings,
    // then use the bounded strict parser so malformed configs default to preload.
    std::string text;
    for (const char* p = config; *p;) {
        if (*p == '"') {
            const char* end = JsonSkipString(p);
            text.append(p, end); p = end;
        } else if (p[0] == '/' && p[1] == '/') {
            while (*p && *p != '\n') ++p;
            text += ' ';
        } else if (p[0] == '/' && p[1] == '*') {
            p += 2;
            while (*p && !(p[0] == '*' && p[1] == '/')) ++p;
            if (!*p) return true;
            p += 2; text += ' ';
        } else { text += *p++; }
    }
    for (std::size_t i = 0; i < text.size();) {
        if (text[i] == '"') { i = JsonSkipString(text.c_str() + i) - text.c_str(); continue; }
        if (text[i] == ',') {
            const char next = *JsonTextSkipWs(text.c_str() + i + 1);
            if (next == '}' || next == ']') text[i] = ' ';
        }
        ++i;
    }
    VpkConfigVisitor visitor;
    return !JsonParse(text.c_str(), visitor).ok || *JsonTextSkipWs(text.c_str()) != '{' || visitor.preload;
}
inline bool VpkMatchesMount(const std::string& stem, const char* mount) {
    if (!mount) return false;
    const char* base = mount;
    for (const char* p = mount; *p; ++p) if (*p == '/' || *p == '\\') base = p + 1;
    return stem == base;
}
}
