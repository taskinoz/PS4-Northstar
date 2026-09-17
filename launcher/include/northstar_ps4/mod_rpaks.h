#pragma once
#include "json_text.h"
#include "mod_catalog.h"
#include <cstring>
#include <string>
#include <vector>

// Northstar mod rpaks: `<mod>/paks/*.rpak` plus a `paks/rpak.json` saying when
// each one loads.
//
// PC's rules, from ModManager's rpak block and PakLoadManager:
//   - `Preload[<pak>] == true`  -> load it early, before the engine's own pak.
//   - else `Postload[<pak>]`    -> a vanilla pak name; load ours right after
//                                  that one loads.
//   - else `<pak>: "<regex>"`   -> legacy map-name regex, deprecated upstream.
// Only one method applies per pak, in that order, so a pak listed under both
// Preload and Postload preloads and the Postload entry is ignored.
//
// Mod rpaks are texture and material archives in practice: both of the ones
// Northstar.Custom ships hold only `txtr` and `matl` assets and no models.
namespace northstar::ps4::mods {

enum class RpakLoadKind { None, Preload, Postload };

struct RpakRule {
    std::string pak;    // file name, e.g. "northstarEventModels.rpak"
    std::string after;  // the vanilla pak it follows, when Postload
    RpakLoadKind kind = RpakLoadKind::None;
};

// PC parses rpak.json with RapidJSON's comment and trailing-comma flags. The
// strict parser here would reject both, so they are blanked first - outside
// strings, so a comma or a `//` inside a value is left alone.
inline std::string RelaxJsonToStrict(const char* config) {
    std::string text;
    if (!config) return text;
    for (const char* p = config; *p;) {
        if (*p == '"') {
            const char* end = JsonSkipString(p);
            if (!end || end == p) { text += *p++; continue; }
            text.append(p, end);
            p = end;
        } else if (p[0] == '/' && p[1] == '/') {
            while (*p && *p != '\n') ++p;
            text += ' ';
        } else if (p[0] == '/' && p[1] == '*') {
            p += 2;
            while (*p && !(p[0] == '*' && p[1] == '/')) ++p;
            if (!*p) break;
            p += 2;
            text += ' ';
        } else {
            text += *p++;
        }
    }
    for (std::size_t i = 0; i < text.size();) {
        if (text[i] == '"') {
            const char* end = JsonSkipString(text.c_str() + i);
            if (!end) break;
            i = static_cast<std::size_t>(end - text.c_str());
            continue;
        }
        if (text[i] == ',') {
            const char next = *JsonTextSkipWs(text.c_str() + i + 1);
            if (next == '}' || next == ']') text[i] = ' ';
        }
        ++i;
    }
    return text;
}

// Collects the Preload and Postload members. Anything else in the file, such
// as Aliases or a legacy regex entry, is skipped rather than treated as an
// error, matching PC's member-at-a-time reading.
struct RpakConfigVisitor {
    int depth = 0;
    int section = 0;  // 1 = Preload, 2 = Postload
    std::string pending;
    std::vector<std::pair<std::string, bool>> preload;
    std::vector<std::pair<std::string, std::string>> postload;

    bool OnObjectBegin() { ++depth; return true; }
    bool OnObjectEnd() {
        --depth;
        if (depth <= 1) section = 0;
        pending.clear();
        return true;
    }
    bool OnArrayBegin() { ++depth; return true; }
    bool OnArrayEnd() { --depth; return true; }
    bool OnKey(const char* key, std::size_t size) {
        const std::string name(key, size);
        if (depth == 1) {
            section = name == "Preload" ? 1 : name == "Postload" ? 2 : 0;
            pending.clear();
        } else if (depth == 2 && section != 0) {
            pending = name;
        }
        return true;
    }
    bool OnBool(bool value) {
        if (depth == 2 && section == 1 && !pending.empty()) preload.emplace_back(pending, value);
        pending.clear();
        return true;
    }
    bool OnString(const char* text, std::size_t size) {
        if (depth == 2 && section == 2 && !pending.empty())
            postload.emplace_back(pending, std::string(text, size));
        pending.clear();
        return true;
    }
    bool OnNull() { pending.clear(); return true; }
    bool OnInteger(int) { return OnNull(); }
    bool OnFloat(float) { return OnNull(); }
};

inline bool IsRpakFileName(const char* name) {
    if (!name || std::strchr(name, '/') || std::strchr(name, '\\') || std::strchr(name, ':')) return false;
    const std::string text(name);
    if (text.find("..") != std::string::npos) return false;
    const std::string suffix(".rpak");
    return text.size() > suffix.size() &&
        !text.compare(text.size() - suffix.size(), suffix.size(), suffix);
}

// Decides how one pak loads. An absent or malformed config leaves every pak
// with no rule, which is PC's behaviour too: it warns and the pak stays
// unloaded rather than being loaded at a guessed time.
inline RpakRule ResolveRpakRule(const std::string& pak, const char* config, bool& configUsable) {
    RpakRule rule;
    rule.pak = pak;
    configUsable = false;
    if (!config) return rule;
    const std::string text = RelaxJsonToStrict(config);
    if (*JsonTextSkipWs(text.c_str()) != '{') return rule;
    RpakConfigVisitor visitor;
    if (!JsonParse(text.c_str(), visitor).ok) return rule;
    configUsable = true;
    for (const auto& entry : visitor.preload) {
        if (entry.first == pak && entry.second) {
            rule.kind = RpakLoadKind::Preload;
            return rule;
        }
    }
    for (const auto& entry : visitor.postload) {
        if (entry.first == pak && !entry.second.empty()) {
            rule.kind = RpakLoadKind::Postload;
            rule.after = entry.second;
            return rule;
        }
    }
    return rule;
}

// The pak worker has a `/app0/r2/paks/PS4/%s` format string, but it is not
// applied on the path a load request actually takes: a relative name went to
// the filesystem verbatim and the open failed on the literal
// `../../../R2Northstar/...`. An absolute path is opened as given, so mod paks
// are requested by their full path.
inline std::string ModRpakRequestPath(const std::string& modName, const std::string& pak) {
    return "/app0/R2Northstar/mods/" + modName + "/paks/" + pak;
}

// The engine asks for paks by bare name, but compare on the basename so a
// request carrying a directory still matches.
inline bool RpakNameMatches(const std::string& expected, const char* requested) {
    if (!requested || expected.empty()) return false;
    const char* base = requested;
    for (const char* p = requested; *p; ++p)
        if (*p == '/' || *p == '\\') base = p + 1;
    return expected == base;
}
}  // namespace northstar::ps4::mods
