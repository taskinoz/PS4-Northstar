#pragma once
#include "mod_catalog.h"
#include <string>
namespace northstar::ps4::mods {
// Preserve unrelated mods/versions and metadata when updating the PC v1 file.
// Escaped identifiers are rejected rather than writing an unmatchable key with
// the current catalog parser.
inline bool SettingsKeySupported(const char* key) {
    if (!key || !*key) return false;
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(key); *p; ++p)
        if (*p < 32 || *p == '"' || *p == '\\') return false;
    return true;
}
inline bool ReplaceSettingsMember(std::string& object, const char* key, const std::string& value) {
    if (!SettingsKeySupported(key)) return false;
    const char* begin = object.c_str();
    const char* start = JsonSkipWs(begin);
    if (*start != '{') return false;
    const char* end = JsonSkipValue(start);
    if (end <= start || end[-1] != '}' || *JsonSkipWs(end)) return false;
    if (const char* old = JsonFindMember(start, key)) {
        const char* oldEnd = JsonSkipValue(old);
        if (oldEnd <= old) return false;
        object.replace(static_cast<std::size_t>(old - begin), static_cast<std::size_t>(oldEnd - old), value);
    } else {
        const bool empty = *JsonSkipWs(start + 1) == '}';
        const std::string member = std::string("\"") + key + "\":" + value + (empty ? "" : ",");
        object.insert(static_cast<std::size_t>(start + 1 - begin), member);
    }
    return true;
}
inline bool SetEnabledSetting(std::string& settings, const char* name, const char* version, bool enabled) {
    if (!SettingsKeySupported(name) || !SettingsKeySupported(version) || std::strcmp(name, "Version") == 0) return false;
    std::string next = settings;
    std::string versions = "{}";
    if (const char* old = JsonFindMember(next.c_str(), name)) {
        old = JsonSkipWs(old);
        if (*old == '{') versions.assign(old, JsonSkipValue(old));
    }
    if (!ReplaceSettingsMember(versions, version, enabled ? "true" : "false") ||
        !ReplaceSettingsMember(next, name, versions) || !ReplaceSettingsMember(next, "Version", "1")) return false;
    settings.swap(next);
    return true;
}
}
