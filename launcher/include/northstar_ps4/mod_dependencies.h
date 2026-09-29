#pragma once

// mod.json "Dependencies" and "PluginDependencies": script constants a mod
// compiles against to use another mod or plugin only when it is there, e.g.
//   "Dependencies": { "HAS_MOD_SETTINGS": "Mod Settings" }
//   #if HAS_MOD_SETTINGS ... #endif
// PC (mods/mod.cpp ParseDependencies, modmanager.cpp, squirrel.cpp VMCreated):
//   - every mod's constants are registered, enabled or not;
//   - the first mod to register a name keeps it, and a later mod naming a
//     different target is an error (PC then fails that mod);
//   - each VM defines the constant as whether an enabled mod has that Name;
//   - a plugin constant is whether that plugin is loaded. No plugin can load on
//     PS4, so they are all false.
// Host-tested in tests/mod_dependencies.cpp.

#include "northstar_ps4/mod_catalog.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace northstar::ps4::mods {

struct ModDependencyInfo {
    std::string name;
    bool enabled = false;
    std::vector<std::pair<std::string, std::string>> dependencies; // constant -> mod Name
    std::vector<std::string> pluginDependencies;                    // constant
};

inline std::string JsonStringValue(const char* p) {
    char buffer[256];
    return JsonExtractString(p, buffer, sizeof(buffer)) ? std::string(buffer) : std::string();
}

// The two fields of one mod.json. Entries that are not strings are skipped,
// as PC skips them.
inline void ParseModDependencies(const char* json, ModDependencyInfo& out) {
    if (const char* deps = JsonFindMember(json, "Dependencies")) {
        const char* p = JsonSkipWs(deps);
        if (*p == '{') {
            p = JsonSkipWs(p + 1);
            while (*p == '"') {
                const std::string constant = JsonStringValue(p);
                p = JsonSkipWs(JsonSkipString(p));
                if (*p != ':') break;
                p = JsonSkipWs(p + 1);
                const bool isString = *p == '"';
                const std::string target = isString ? JsonStringValue(p) : std::string();
                p = JsonSkipWs(JsonSkipValue(p));
                if (isString && !constant.empty()) {
                    bool known = false;
                    for (const auto& existing : out.dependencies)
                        if (existing.first == constant) known = true;
                    if (!known) out.dependencies.emplace_back(constant, target);
                }
                if (*p != ',') break;
                p = JsonSkipWs(p + 1);
            }
        }
    }
    if (const char* plugins = JsonFindMember(json, "PluginDependencies")) {
        const char* p = JsonSkipWs(plugins);
        if (*p == '[') {
            p = JsonSkipWs(p + 1);
            while (*p && *p != ']') {
                if (*p == '"') {
                    const std::string constant = JsonStringValue(p);
                    if (!constant.empty()) out.pluginDependencies.push_back(constant);
                }
                p = JsonSkipWs(JsonSkipValue(p));
                if (*p != ',') break;
                p = JsonSkipWs(p + 1);
            }
        }
    }
}

struct DependencyConstants {
    std::vector<std::pair<std::string, std::int64_t>> values;
    // "<mod> registers <constant> for <target>, already registered for <other>".
    std::vector<std::string> conflicts;
};

// `mods` in load order.
inline DependencyConstants ResolveDependencyConstants(const std::vector<ModDependencyInfo>& mods) {
    DependencyConstants out;
    std::vector<std::pair<std::string, std::string>> registered;
    std::vector<std::string> plugins;
    for (const auto& mod : mods) {
        for (const auto& dependency : mod.dependencies) {
            bool found = false;
            for (const auto& existing : registered) {
                if (existing.first != dependency.first) continue;
                found = true;
                if (existing.second != dependency.second)
                    out.conflicts.push_back(mod.name + " registers " + dependency.first + " for " + dependency.second +
                        ", already registered for " + existing.second);
            }
            if (!found) registered.push_back(dependency);
        }
        for (const auto& plugin : mod.pluginDependencies) {
            bool found = false;
            for (const auto& existing : plugins) found = found || existing == plugin;
            if (!found) plugins.push_back(plugin);
        }
    }
    for (const auto& constant : registered) {
        bool enabled = false;
        for (const auto& mod : mods) enabled = enabled || (mod.enabled && mod.name == constant.second);
        out.values.emplace_back(constant.first, enabled ? 1 : 0);
    }
    for (const auto& plugin : plugins) {
        bool known = false;
        for (const auto& value : out.values) known = known || value.first == plugin;
        if (!known) out.values.emplace_back(plugin, 0);
    }
    return out;
}

} // namespace northstar::ps4::mods
