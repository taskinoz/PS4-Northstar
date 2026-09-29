#include "northstar_ps4/mod_rpaks.h"
#include "northstar_ps4/rpak_texture_converter.h"
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
    assert(ModRpakRequestPath("/app0/R2Northstar/mods/Northstar.Custom", "a.rpak") ==
        "/app0/R2Northstar/mods/Northstar.Custom/paks/a.rpak");

    // --- name matching ------------------------------------------------------
    assert(RpakNameMatches("common.rpak", "common.rpak"));
    assert(RpakNameMatches("common.rpak", "some/dir/common.rpak"));
    assert(!RpakNameMatches("common.rpak", "common_mp.rpak"));
    assert(!RpakNameMatches("common.rpak", nullptr));
    assert(!RpakNameMatches("", "common.rpak"));

    // --- embedded STARPak discovery and redirected-path matching -----------
    std::string header(0x58, '\0');
    std::memcpy(header.data(), "RPak", 4);
    header[4] = 7;
    const std::string streamNames("weapon.starpak\0nested/event.starpak\0", 37);
    header[0x38] = static_cast<char>(streamNames.size());
    header += streamNames;
    std::vector<std::string> references;
    assert(ParseRpakStarpakReferences(header.data(), header.size(), references));
    assert(references.size() == 2);
    assert(references[0] == "weapon.starpak");
    assert(references[1] == "nested/event.starpak");
    assert(RpakStreamPathMatches(references[0], "/app0/r2/weapon.starpak"));
    assert(RpakStreamPathMatches(references[1], "r2\\nested\\EVENT.STARPAK"));
    assert(!RpakStreamPathMatches(references[0], "/app0/r2/other.starpak"));

    std::string traversal = header.substr(0, 0x58);
    const std::string bad("../outside.starpak\0", 19);
    traversal[0x38] = static_cast<char>(bad.size());
    traversal += bad;
    assert(!ParseRpakStarpakReferences(traversal.data(), traversal.size(), references));
    assert(!ParseRpakStarpakReferences("bad", 3, references));

    // --- bounded texture-platform inspection -------------------------------
    // One slab, one page and one txtr descriptor. The texture header starts
    // at page offset zero and carries the PS4 layout marker at +0x1c.
    std::string texturePak(0x58, '\0');
    std::memcpy(texturePak.data(), "RPak", 4);
    texturePak[4] = 7;
    texturePak[0x3a] = 1;  // slab count
    texturePak[0x3c] = 1;  // page count
    texturePak[0x44] = 1;  // asset count
    texturePak.resize(0x58 + 16 + 12 + 72 + 0x38, '\0');
    const std::size_t pageHeader = 0x58 + 16;
    texturePak[pageHeader + 8] = 0x38;
    const std::size_t assetHeader = pageHeader + 12;
    texturePak[assetHeader + 60] = 0x38;
    std::memcpy(texturePak.data() + assetHeader + 68, "txtr", 4);
    const std::size_t textureHeader = assetHeader + 72;
    texturePak[textureHeader + 0x1c] = 8;
    RpakTexturePlatforms platforms;
    assert(InspectRpakTexturePlatforms(texturePak.data(), texturePak.size(), platforms));
    assert(platforms.textures == 1 && platforms.pc == 0 && platforms.ps4 == 1 && platforms.other == 0);
    assert(IsSupportedPs4TextureRpak(platforms));
    texturePak[textureHeader + 0x1c] = 0;
    assert(InspectRpakTexturePlatforms(texturePak.data(), texturePak.size(), platforms));
    assert(platforms.textures == 1 && platforms.pc == 1 && platforms.ps4 == 0);
    assert(!IsSupportedPs4TextureRpak(platforms));
    platforms = {};
    assert(!IsSupportedPs4TextureRpak(platforms));
    texturePak.resize(texturePak.size() - 1);
    assert(!InspectRpakTexturePlatforms(texturePak.data(), texturePak.size(), platforms));

    // --- PC -> PS4 texture conversion --------------------------------------
    // Three BC1 mips (16, 8 and 4 pixels) occupy 176 bytes in RePak's linear
    // layout. PS4 stores every mip as a complete 8x8-block tile: 512 bytes
    // each. The conversion therefore grows this page to 1536 bytes.
    std::vector<std::uint8_t> convertible(0x58, 0);
    std::memcpy(convertible.data(), "RPak", 4);
    convertible[4] = 7;
    convertible[0x3a] = 2;  // slabs
    convertible[0x3c] = 2;  // pages
    convertible[0x44] = 1;  // assets
    const std::size_t converterSlabs = convertible.size();
    convertible.resize(convertible.size() + 2 * 16, 0);
    const std::size_t converterPages = convertible.size();
    convertible.resize(convertible.size() + 2 * 12, 0);
    const std::size_t converterAsset = convertible.size();
    convertible.resize(convertible.size() + 72, 0);
    const std::size_t converterData = convertible.size();
    RpakWriteU32(convertible.data() + converterPages + 8, 56);
    RpakWriteU32(convertible.data() + converterPages + 12, 1);  // slab 1
    RpakWriteU32(convertible.data() + converterPages + 12 + 8, 176);
    RpakWriteU64(convertible.data() + converterSlabs + 8, 56);
    RpakWriteU64(convertible.data() + converterSlabs + 16 + 8, 176);
    RpakWriteU32(convertible.data() + converterAsset + 16, 0);
    RpakWriteU32(convertible.data() + converterAsset + 20, 0);
    RpakWriteU32(convertible.data() + converterAsset + 24, 1);
    RpakWriteU32(convertible.data() + converterAsset + 28, 0);
    RpakWriteU64(convertible.data() + converterAsset + 32, ~std::uint64_t(0));
    RpakWriteU32(convertible.data() + converterAsset + 60, 56);
    RpakWriteU32(convertible.data() + converterAsset + 64, 8);
    std::memcpy(convertible.data() + converterAsset + 68, "txtr", 4);
    convertible.resize(converterData + 56 + 176, 0);
    std::uint8_t* converterHeader = convertible.data() + converterData;
    RpakWriteU16(converterHeader + 16, 16);
    RpakWriteU16(converterHeader + 18, 16);
    RpakWriteU16(converterHeader + 22, 0);  // BC1
    RpakWriteU32(converterHeader + 24, 176);
    converterHeader[30] = 1;
    converterHeader[33] = 3;
    for (std::size_t i = converterData + 56; i < convertible.size(); ++i)
        convertible[i] = static_cast<std::uint8_t>(i);
    const std::uint8_t largestMipFirstByte = convertible[converterData + 56 + 48];
    RpakWriteU64(convertible.data() + 0x18, convertible.size());
    RpakWriteU64(convertible.data() + 0x28, convertible.size());
    std::vector<std::vector<std::uint8_t>> noStarpaks;
    RpakPs4ConversionReport conversion;
    std::string conversionError;
    assert(ConvertRpakTexturesToPs4(convertible, noStarpaks, conversion, conversionError));
    assert(conversion.textures == 1 && conversion.permanentBytesBefore == 176 &&
        conversion.permanentBytesAfter == 1536 && conversion.streamedBlocks == 0);
    assert(convertible.size() == converterData + 56 + 1536);
    assert(RpakReadU32(convertible.data() + converterPages + 12 + 8) == 1536);
    assert(RpakReadU64(convertible.data() + converterSlabs + 16 + 8) == 1536);
    converterHeader = convertible.data() + converterData;
    assert(converterHeader[28] == 8 && converterHeader[32] == 1);
    assert(RpakReadU32(converterHeader + 24) == 1536);
    assert(convertible[converterData + 56] == largestMipFirstByte);  // PS4 top-to-bottom order
    assert(RpakReadU64(convertible.data() + 0x18) == convertible.size());
    assert(RpakReadU64(convertible.data() + 0x28) == convertible.size());

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
    if (argc >= 3) {
        const std::string livePak = ReadFile(argv[2]);
        if (livePak.empty() || !ParseRpakStarpakReferences(
                livePak.data(), livePak.size(), references) || references.empty()) {
            std::fprintf(stderr, "live-rpak check: could not read stream references from %s\n", argv[2]);
            return 1;
        }
        std::printf("rpak live-stream check passed: %zu reference(s), first=%s\n",
            references.size(), references[0].c_str());
        if (!InspectRpakTexturePlatforms(livePak.data(), livePak.size(), platforms)) {
            std::fprintf(stderr, "live-rpak check: could not inspect texture platforms in %s\n", argv[2]);
            return 1;
        }
        std::printf("rpak live-texture check passed: textures=%zu pc=%zu ps4=%zu other=%zu\n",
            platforms.textures, platforms.pc, platforms.ps4, platforms.other);
    }
    return 0;
}
