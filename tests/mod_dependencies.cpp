#include "northstar_ps4/mod_dependencies.h"
#include <cstdio>
#include <cstdlib>
using namespace northstar::ps4::mods;

static void Check(bool ok, const char* what, int line) {
    if (!ok) {
        std::fprintf(stderr, "line %d: %s\n", line, what);
        std::exit(1);
    }
}
#define CHECK(x) Check((x), #x, __LINE__)

static ModDependencyInfo Mod(const char* name, bool enabled, const char* json) {
    ModDependencyInfo info;
    info.name = name;
    info.enabled = enabled;
    ParseModDependencies(json, info);
    return info;
}

static std::int64_t Value(const DependencyConstants& constants, const char* name) {
    for (const auto& value : constants.values)
        if (value.first == name) return value.second;
    return -1;
}

int main() {
    // HUD Revamp's and AutoGG's shapes.
    auto hud = Mod("HUD Revamp", true, R"({
        "Name": "HUD Revamp",
        // comment
        "Dependencies": { "HAS_MOD_SETTINGS": "Mod Settings", "HAS_FRAMEWORK": "Titan Framework", "BAD": 3 },
        "Scripts": []
    })");
    CHECK(hud.dependencies.size() == 2);
    CHECK(hud.dependencies[0].first == "HAS_MOD_SETTINGS" && hud.dependencies[0].second == "Mod Settings");
    auto gg = Mod("AutoGG", true, R"({"Name":"AutoGG","PluginDependencies":["DISCORDRPC", 5, "OTHER_PLUGIN"],
        "Dependencies":{"HAS_MOD_SETTINGS":"Mod Settings"}})");
    CHECK(gg.pluginDependencies.size() == 2 && gg.pluginDependencies[1] == "OTHER_PLUGIN");
    auto settings = Mod("Mod Settings", true, R"({"Name":"Mod Settings"})");
    auto framework = Mod("Titan Framework", false, R"({"Name":"Titan Framework"})");
    auto clash = Mod("Other", true, R"({"Name":"Other","Dependencies":{"HAS_FRAMEWORK":"Something Else"}})");

    auto constants = ResolveDependencyConstants({hud, gg, settings, framework, clash});
    CHECK(Value(constants, "HAS_MOD_SETTINGS") == 1);
    CHECK(Value(constants, "HAS_FRAMEWORK") == 0); // installed but disabled
    CHECK(Value(constants, "DISCORDRPC") == 0);
    CHECK(Value(constants, "OTHER_PLUGIN") == 0);
    CHECK(constants.values.size() == 4);
    CHECK(constants.conflicts.size() == 1);

    // A constant registered by a disabled mod still exists.
    auto off = Mod("Off", false, R"({"Dependencies":{"HAS_OFF_TARGET":"Missing Mod"}})");
    constants = ResolveDependencyConstants({off});
    CHECK(Value(constants, "HAS_OFF_TARGET") == 0);

    CHECK(ResolveDependencyConstants({Mod("Plain", true, R"({"Name":"Plain"})")}).values.empty());

    std::puts("mod_dependencies tests passed");
    return 0;
}
