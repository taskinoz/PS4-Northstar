#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace northstar::ps4::mods {
constexpr std::size_t kMaxModConVars = 32;
constexpr std::size_t kMaxModNames = 128;
constexpr std::size_t kModJsonBufferSize = 16 * 1024;

inline const char* JsonSkipWs(const char* p) noexcept {
    for (;;) {
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') ++p;
        if (p[0] == '/' && p[1] == '/') {
            while (*p && *p != '\n') ++p;
        } else if (p[0] == '/' && p[1] == '*') {
            p += 2;
            while (*p && !(p[0] == '*' && p[1] == '/')) ++p;
            if (*p) p += 2;
        } else break;
    }
    return p;
}

inline const char* JsonSkipString(const char* p) noexcept {
    if (*p != '"') return p;
    ++p;
    while (*p != '\0') {
        if (*p == '\\' && p[1] != '\0') {
            p += 2;
            continue;
        }
        if (*p == '"') return p + 1;
        ++p;
    }
    return p;
}

inline const char* JsonSkipValue(const char* p) noexcept {
    p = JsonSkipWs(p);
    if (*p == '"') return JsonSkipString(p);
    if (*p == '{') {
        ++p;
        for (;;) {
            p = JsonSkipWs(p);
            if (*p == '}') return p + 1;
            if (*p != '"') return p;
            p = JsonSkipString(p);
            p = JsonSkipWs(p);
            if (*p != ':') return p;
            p = JsonSkipValue(p + 1);
            p = JsonSkipWs(p);
            if (*p == ',') {
                ++p;
                continue;
            }
            return (*p == '}') ? p + 1 : p;
        }
    }
    if (*p == '[') {
        ++p;
        for (;;) {
            p = JsonSkipWs(p);
            if (*p == ']') return p + 1;
            p = JsonSkipValue(p);
            p = JsonSkipWs(p);
            if (*p == ',') {
                ++p;
                continue;
            }
            return (*p == ']') ? p + 1 : p;
        }
    }
    while (*p != '\0' && *p != ',' && *p != '}' && *p != ']') ++p;
    return p;
}

inline const char* JsonFindMember(const char* object, const char* key) noexcept {
    if (object == nullptr) return nullptr;
    const char* p = JsonSkipWs(object);
    if (*p != '{') return nullptr;
    ++p;
    for (;;) {
        p = JsonSkipWs(p);
        if (*p != '"') return nullptr;
        const char* const keyStart = p + 1;
        const char* keyEnd = keyStart;
        while (*keyEnd != '\0' && *keyEnd != '"') {
            if (*keyEnd == '\\' && keyEnd[1] != '\0') ++keyEnd;
            ++keyEnd;
        }
        if (*keyEnd != '"') return nullptr;
        const std::size_t keyLength =
            static_cast<std::size_t>(keyEnd - keyStart);
        const bool matches = std::strlen(key) == keyLength &&
            std::memcmp(keyStart, key, keyLength) == 0;
        p = keyEnd + 1;
        p = JsonSkipWs(p);
        if (*p != ':') return nullptr;
        p = JsonSkipWs(p + 1);
        if (matches) return p;
        p = JsonSkipValue(p);
        p = JsonSkipWs(p);
        if (*p != ',') return nullptr;
        ++p;
    }
}

inline bool JsonExtractString(const char* p, char* out, std::size_t capacity) noexcept {
    p = JsonSkipWs(p);
    if (*p != '"') return false;
    ++p;
    std::size_t used = 0;
    while (*p != '\0' && *p != '"') {
        if (*p == '\\' && p[1] != '\0') {
            ++p;
            char decoded = *p;
            switch (*p) {
                case 'b': decoded = '\b'; break;
                case 'f': decoded = '\f'; break;
                case 'n': decoded = '\n'; break;
                case 'r': decoded = '\r'; break;
                case 't': decoded = '\t'; break;
                default: break;
            }
            if (used + 1 >= capacity) return false;
            out[used++] = decoded;
            ++p;
            continue;
        }
        if (used + 1 >= capacity) return false;
        out[used++] = *p;
        ++p;
    }
    if (capacity > 0) out[used] = '\0';
    return *p == '"';
}

inline long long JsonExtractInteger(const char* p) noexcept {
    p = JsonSkipWs(p);
    bool negative = false;
    if (*p == '-') {
        negative = true;
        ++p;
    }
    long long value = 0;
    if (*p < '0' || *p > '9') return 0;
    while (*p >= '0' && *p <= '9') {
        value = value * 10 + (*p - '0');
        ++p;
    }
    return negative ? -value : value;
}

constexpr std::size_t kMaxModUiScripts = 32;
constexpr std::size_t kMaxModLocalisationFiles = 16;

struct ModConVarInfo {
    char name[64];
    char defaultValue[64];
    char flags[32];
};

struct ModInfo {
    char name[64];
    char description[128];
    char version[32];
    char initScript[96];
    char initScriptCallback[96];
    std::int32_t loadPriority = 0;
    std::int32_t scriptCount = 0;
    std::int32_t conVarCount = 0;
    ModConVarInfo conVars[kMaxModConVars];
    // Subset of Scripts[] whose "RunOn" is exactly "UI" (the only CompileList
    // context proven safe so far, VA-verified as countTable index 2 by
    // ProbeUiScriptSystem). CLIENT/SERVER/compound RunOn expressions are
    // counted in scriptCount but intentionally not collected here until
    // their CompileList context index is identified the same way.
    std::int32_t uiScriptCount = 0;
    char uiScripts[kMaxModUiScripts][96];
    // Localisation[] file paths (PC mod.json key, e.g.
    // "resource/northstar_client_localisation_%language%.txt"). Fed to the
    // game's CLocalise::AddFile so mod tokens load through the native
    // localise interface instead of the engine VPK bake.
    std::int32_t localisationCount = 0;
    char localisationFiles[kMaxModLocalisationFiles][160];
};

struct ModDiscovery {
    char names[kMaxModNames][64];
    std::int32_t priorities[kMaxModNames]{};
    std::int32_t count = 0;
};

inline bool ParseModMetadata(const char* json, ModInfo& out) noexcept {
    out = ModInfo{};
    const char* const nameValue = JsonFindMember(json, "Name");
    if (nameValue != nullptr) {
        if (!JsonExtractString(nameValue, out.name, sizeof(out.name))) return false;
    }
    const char* const descriptionValue = JsonFindMember(json, "Description");
    if (descriptionValue != nullptr) {
        JsonExtractString(descriptionValue, out.description,
            sizeof(out.description));
    }
    std::strcpy(out.version, "0.0.0");
    const char* const versionValue = JsonFindMember(json, "Version");
    if (versionValue != nullptr) {
        if (!JsonExtractString(versionValue, out.version, sizeof(out.version))) return false;
    }
    const char* const initScriptValue = JsonFindMember(json, "InitScript");
    if (initScriptValue != nullptr) {
        const char* path = initScriptValue;
        if (*JsonSkipWs(path) == '{') {
            path = JsonFindMember(initScriptValue, "InitScript");
            const char* callback = JsonFindMember(initScriptValue, "InitScriptCallback");
            if (callback != nullptr && !JsonExtractString(callback,
                    out.initScriptCallback, sizeof(out.initScriptCallback))) return false;
        }
        if (path == nullptr || !JsonExtractString(path, out.initScript,
                sizeof(out.initScript))) return false;
    }
    const char* const priorityValue = JsonFindMember(json, "LoadPriority");
    if (priorityValue != nullptr) {
        out.loadPriority =
            static_cast<std::int32_t>(JsonExtractInteger(priorityValue));
    }

    const char* const conVarsValue = JsonFindMember(json, "ConVars");
    if (conVarsValue != nullptr) {
        const char* elem = JsonSkipWs(conVarsValue);
        if (*elem == '[') elem = JsonSkipWs(elem + 1);
        while (elem != nullptr && *elem != ']' &&
            out.conVarCount < static_cast<std::int32_t>(kMaxModConVars)) {
            if (*elem == '{') {
                ModConVarInfo& info = out.conVars[out.conVarCount];
                const char* const nv = JsonFindMember(elem, "Name");
                if (nv != nullptr &&
                    JsonExtractString(nv, info.name, sizeof(info.name))) {
                    const char* const dv =
                        JsonFindMember(elem, "DefaultValue");
                    if (dv != nullptr) {
                        JsonExtractString(dv, info.defaultValue,
                            sizeof(info.defaultValue));
                    }
                    const char* const fv = JsonFindMember(elem, "Flags");
                    if (fv != nullptr) {
                        JsonExtractString(fv, info.flags, sizeof(info.flags));
                    }
                    ++out.conVarCount;
                }
            }
            elem = JsonSkipWs(JsonSkipValue(elem));
            if (*elem == ',') elem = JsonSkipWs(elem + 1);
            else break;
        }
    }

    const char* const scriptsValue = JsonFindMember(json, "Scripts");
    if (scriptsValue != nullptr) {
        const char* elem = JsonSkipWs(scriptsValue);
        if (*elem == '[') elem = JsonSkipWs(elem + 1);
        while (elem != nullptr && *elem != ']') {
            ++out.scriptCount;
            if (*elem == '{') {
                const char* const pathValue = JsonFindMember(elem, "Path");
                const char* const runOnValue = JsonFindMember(elem, "RunOn");
                char path[96]{};
                char runOn[64]{};
                const bool havePath = pathValue != nullptr &&
                    JsonExtractString(pathValue, path, sizeof(path));
                const bool haveUiRunOn = runOnValue != nullptr &&
                    JsonExtractString(runOnValue, runOn, sizeof(runOn)) &&
                    std::strcmp(runOn, "UI") == 0;
                if (havePath && haveUiRunOn &&
                    out.uiScriptCount < static_cast<std::int32_t>(kMaxModUiScripts)) {
                    std::strncpy(out.uiScripts[out.uiScriptCount], path,
                        sizeof(out.uiScripts[0]) - 1);
                    out.uiScripts[out.uiScriptCount][sizeof(out.uiScripts[0]) - 1] = '\0';
                    ++out.uiScriptCount;
                }
            }
            elem = JsonSkipWs(JsonSkipValue(elem));
            if (*elem == ',') elem = JsonSkipWs(elem + 1);
            else break;
        }
    }

    const char* const localisationValue = JsonFindMember(json, "Localisation");
    if (localisationValue != nullptr) {
        const char* elem = JsonSkipWs(localisationValue);
        if (*elem == '[') elem = JsonSkipWs(elem + 1);
        while (elem != nullptr && *elem != ']' &&
            out.localisationCount <
                static_cast<std::int32_t>(kMaxModLocalisationFiles)) {
            if (*elem == '"') {
                char file[160]{};
                if (JsonExtractString(elem, file, sizeof(file))) {
                    std::strncpy(out.localisationFiles[out.localisationCount], file,
                        sizeof(out.localisationFiles[0]) - 1);
                    out.localisationFiles[out.localisationCount]
                        [sizeof(out.localisationFiles[0]) - 1] = '\0';
                    ++out.localisationCount;
                }
            }
            elem = JsonSkipWs(JsonSkipValue(elem));
            if (*elem == ',') elem = JsonSkipWs(elem + 1);
            else break;
        }
    }
    return out.name[0] != '\0';
}

// PC enabledmods.json v1 uses metadata Name -> Version -> boolean.
// Legacy Name -> boolean is accepted without rewriting the user's file.
inline bool IsModEnabled(const char* settings, const ModInfo& mod) noexcept {
    const char* value = JsonFindMember(settings, mod.name);
    if (value == nullptr) return true;
    if (*JsonSkipWs(value) == '{') value = JsonFindMember(value, mod.version);
    if (value == nullptr) return true;
    value = JsonSkipWs(value);
    return std::strncmp(value, "true", 4) == 0 &&
        (*JsonSkipWs(value + 4) == ',' || *JsonSkipWs(value + 4) == '}');
}

inline bool IsModFolderName(const char* name) noexcept {
    return name != nullptr && name[0] != '.' && name[0] != '\0' &&
        std::strlen(name) < sizeof(ModDiscovery{}.names[0]) &&
        std::strpbrk(name, "/\\:\r\n") == nullptr;
}

// Ascending LoadPriority; stable folder-name tie-break for reproducible boots.
// Filesystem lookup walks this list backwards so higher priority wins.
inline bool InsertMod(ModDiscovery& list, const char* folder, std::int32_t priority) noexcept {
    if (!IsModFolderName(folder) || list.count >= static_cast<std::int32_t>(kMaxModNames)) return false;
    for (std::int32_t i = 0; i < list.count; ++i)
        if (std::strcmp(list.names[i], folder) == 0) return true;
    std::int32_t i = list.count++;
    while (i > 0 && (list.priorities[i - 1] > priority ||
        (list.priorities[i - 1] == priority && std::strcmp(list.names[i - 1], folder) > 0))) {
        std::strcpy(list.names[i], list.names[i - 1]);
        list.priorities[i] = list.priorities[i - 1];
        --i;
    }
    std::strcpy(list.names[i], folder);
    list.priorities[i] = priority;
    return true;
}
} // namespace northstar::ps4::mods
