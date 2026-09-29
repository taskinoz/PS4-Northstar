#include "northstar_ps4/audio_override.h"
#include <cstdio>
#include <cstdlib>
using namespace northstar::ps4::audio;

static void Check(bool ok, const char* what, int line) {
    if (!ok) {
        std::fprintf(stderr, "line %d: %s\n", line, what);
        std::exit(1);
    }
}
#define CHECK(x) Check((x), #x, __LINE__)

static std::unique_ptr<Override> Make(const char* mod, const char* json, std::vector<std::string> samples = {}) {
    auto entry = std::make_unique<Override>();
    entry->mod = mod;
    std::string error;
    Check(ParseDefinition(json, entry->definition, error), json, __LINE__);
    entry->samples = std::move(samples);
    return entry;
}

int main() {
    Definition definition;
    std::string error;
    CHECK(ParseDefinition(R"({ "EventId": "pilot_grapple_fire" })", definition, error));
    CHECK(definition.eventIds.size() == 1 && definition.strategy == Strategy::Sequential);
    CHECK(ParseDefinition(R"({
        // comment, trailing comma
        "EventId": [ "a", "b", ],
        "EventIdRegex": "^diag_.*",
        "AudioSelectionStrategy": "random",
    })", definition, error));
    CHECK(definition.eventIds.size() == 2 && definition.eventRegexes.size() == 1);
    CHECK(definition.strategy == Strategy::Random);
    CHECK(!ParseDefinition(R"({ "EventIdRegex": "x" })", definition, error));
    CHECK(error.find("EventId") != std::string::npos);
    CHECK(!ParseDefinition(R"({ "EventId": [ "a", 3 ] })", definition, error));
    CHECK(!ParseDefinition(R"({ "EventId": "a", "AudioSelectionStrategy": "loud" })", definition, error));
    CHECK(!ParseDefinition(R"([ "a" ])", definition, error));

    CHECK(ShouldOverride("pilot_grapple_fire", {"pilot_grapple_fire"}));
    CHECK(!ShouldOverride("pilot_grapple_fire", {"*", "!pilot_grapple_fire"}));
    CHECK(!ShouldOverride("mp_amb_wind", {"*"}));
    CHECK(!ShouldOverride("x_emit_y", {"*"}));
    CHECK(ShouldOverride("x_emit_y", {"x_emit_y"}));

    Registry registry;
    auto log = registry.Add(Make("First", R"({ "EventId": [ "a", "b" ], "EventIdRegex": "^diag_" })",
        {"audio/first/a/1.wav", "audio/first/a/2.wav"}));
    CHECK(log.empty());
    log = registry.Add(Make("Second", R"({ "EventId": [ "b", "c" ], "EventIdRegex": [ "^diag_", "([" ] })"));
    CHECK(log.size() == 3); // b taken, regex taken, malformed regex
    CHECK(registry.Find("a")->mod == "First");
    CHECK(registry.Find("b")->mod == "First");
    CHECK(registry.Find("c")->mod == "Second");
    CHECK(registry.Find("diag_sp_intro")->mod == "First");
    CHECK(registry.IsClaimed("diag_sp_intro")); // cached
    CHECK(registry.Find("other") == nullptr);
    registry.Add(Make("Wild", R"({ "EventId": "*" })"));
    CHECK(registry.Find("other")->mod == "Wild");
    CHECK(PickSample(*registry.Find("c"), 0) == -1);

    Override& first = *registry.Find("a");
    CHECK(PickSample(first, 0) == 0 && PickSample(first, 0) == 1 && PickSample(first, 0) == 0);
    first.definition.strategy = Strategy::Random;
    CHECK(PickSample(first, 7) == 1);

    CHECK(SampleEventFolder("/app0/mods/X/audio/sound/pilot_grapple_fire/1.wav") == "pilot_grapple_fire");
    CHECK(SampleEventFolder("1.wav").empty());
    CHECK(sizeof(kEmptyWave) == 45 && kEmptyWave[0] == 'R');

    std::puts("audio_override tests passed");
    return 0;
}
