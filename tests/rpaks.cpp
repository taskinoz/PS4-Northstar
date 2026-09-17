#include "northstar_ps4/mod_rpaks.h"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
using namespace northstar::ps4::mods;

static std::string ReadFile(const char* path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return std::string();
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

static RpakRule Rule(const std::string& pak, const char* config, bool expectUsable = true) {
    bool usable = false;
    RpakRule rule = ResolveRpakRule(pak, config, usable);
    assert(usable == expectUsable);
    return rule;
}

int main(int argc, char** argv) {
    // --- file-name filtering ------------------------------------------------
    assert(IsRpakFileName("northstarEventModels.rpak"));
    assert(!IsRpakFileName("notapak.txt"));
    assert(!IsRpakFileName(".rpak"));            // suffix only, no name
    assert(!IsRpakFileName("../escape.rpak"));   // no traversal from a listing
    assert(!IsRpakFileName("sub/dir.rpak"));
    assert(!IsRpakFileName(nullptr));

    // --- Postload -----------------------------------------------------------
    const char* config =
        "{\n"
        "\t\"Postload\": {\n"
        "\t\t\"mp_weapon_shotgun_doublebarrel.rpak\": \"common.rpak\",\n"
        "\t\t\"northstarEventModels.rpak\": \"common.rpak\"\n"
        "\t}\n"
        "}\n";
    RpakRule postload = Rule("northstarEventModels.rpak", config);
    assert(postload.kind == RpakLoadKind::Postload);
    assert(postload.after == "common.rpak");
    // A pak the config says nothing about gets no rule and must not load.
    assert(Rule("unlisted.rpak", config).kind == RpakLoadKind::None);

    // --- Preload wins over Postload ----------------------------------------
    const char* both =
        "{\"Preload\":{\"a.rpak\":true,\"b.rpak\":false},"
        " \"Postload\":{\"a.rpak\":\"common.rpak\",\"b.rpak\":\"common.rpak\"}}";
    assert(Rule("a.rpak", both).kind == RpakLoadKind::Preload);
    // Preload false falls through to the Postload entry, as PC does.
    RpakRule fell = Rule("b.rpak", both);
    assert(fell.kind == RpakLoadKind::Postload && fell.after == "common.rpak");

    // --- members PC ignores stay ignored ------------------------------------
    const char* extra =
        "{\"Aliases\":{\"x.rpak\":\"y.rpak\"},"
        " \"legacy.rpak\":\"mp_.*\","
        " \"Postload\":{\"z.rpak\":\"common.rpak\"}}";
    assert(Rule("z.rpak", extra).kind == RpakLoadKind::Postload);
    // A legacy regex entry is not a Postload rule, so it must not load blind.
    assert(Rule("legacy.rpak", extra).kind == RpakLoadKind::None);
    // An alias key must not be mistaken for a load rule.
    assert(Rule("x.rpak", extra).kind == RpakLoadKind::None);

    // --- comments and trailing commas, which PC's parser flags allow --------
    const char* relaxed =
        "{\n"
        "\t// which paks load when\n"
        "\t\"Postload\": {\n"
        "\t\t\"c.rpak\": \"common.rpak\",   /* after common */\n"
        "\t},\n"
        "}\n";
    RpakRule commented = Rule("c.rpak", relaxed);
    assert(commented.kind == RpakLoadKind::Postload && commented.after == "common.rpak");
    // A `//` inside a string is content, not a comment.
    RpakRule slashes = Rule("d.rpak", "{\"Postload\":{\"d.rpak\":\"a//b.rpak\"}}");
    assert(slashes.after == "a//b.rpak");

    // --- malformed configs are unusable, and grant no rules -----------------
    bool usable = true;
    assert(ResolveRpakRule("a.rpak", "{\"Postload\":", usable).kind == RpakLoadKind::None && !usable);
    assert(ResolveRpakRule("a.rpak", "not json", usable).kind == RpakLoadKind::None && !usable);
    assert(ResolveRpakRule("a.rpak", "[1,2]", usable).kind == RpakLoadKind::None && !usable);
    assert(ResolveRpakRule("a.rpak", nullptr, usable).kind == RpakLoadKind::None && !usable);

    // --- request path -------------------------------------------------------
    // Absolute: a relative name is passed to the filesystem verbatim and fails.
    assert(ModRpakRequestPath("Northstar.Custom", "a.rpak") ==
        "/app0/R2Northstar/mods/Northstar.Custom/paks/a.rpak");

    // --- name matching ------------------------------------------------------
    assert(RpakNameMatches("common.rpak", "common.rpak"));
    assert(RpakNameMatches("common.rpak", "some/dir/common.rpak"));
    assert(!RpakNameMatches("common.rpak", "common_mp.rpak"));
    assert(!RpakNameMatches("common.rpak", nullptr));
    assert(!RpakNameMatches("", "common.rpak"));

    std::puts("rpak config tests passed.");

    // --- the real shipped config, when this test is given it ----------------
    if (argc < 2) {
        std::puts("rpak live-config check skipped (no path given).");
        return 0;
    }
    const std::string live = ReadFile(argv[1]);
    if (live.empty()) {
        std::fprintf(stderr, "live-config check: could not read %s\n", argv[1]);
        return 1;
    }
    for (const char* pak : {"mp_weapon_shotgun_doublebarrel.rpak", "northstarEventModels.rpak"}) {
        bool liveUsable = false;
        const RpakRule rule = ResolveRpakRule(pak, live.c_str(), liveUsable);
        if (!liveUsable || rule.kind != RpakLoadKind::Postload || rule.after != "common.rpak") {
            std::fprintf(stderr, "live-config check: %s resolved to kind=%d after=%s\n",
                pak, static_cast<int>(rule.kind), rule.after.c_str());
            return 1;
        }
    }
    std::puts("rpak live-config check passed: both shipped paks postload after common.rpak.");
    return 0;
}
