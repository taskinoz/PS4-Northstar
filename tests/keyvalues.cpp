#include "northstar_ps4/keyvalues.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
using namespace northstar::ps4::mods;

static KeyValueList Parse(const char* text) {
    KeyValueList list;
    std::string error;
    if (!ParseKeyValues(text, list, error)) {
        std::fprintf(stderr, "PARSE FAILED: %s\nfor: %s\n", error.c_str(), text);
        std::fflush(stderr);
        std::abort();
    }
    return list;
}

static bool ParseFails(const char* text) {
    KeyValueList list;
    std::string error;
    return !ParseKeyValues(text, list, error);
}

static std::size_t CountOccurrences(const std::string& haystack, const std::string& needle) {
    std::size_t count = 0, at = 0;
    while ((at = haystack.find(needle, at)) != std::string::npos) { ++count; at += needle.size(); }
    return count;
}

static std::string ReadFile(const char* path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return std::string();
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

// `--fit <original> <patch>...` merges one real file the way the runtime does
// and reports whether the result still fits. The runtime refuses to serve a
// merged file larger than the original (the engine reads it short), so a file
// that does not fit is silently served as vanilla - worth knowing here rather
// than from a desync mid-match.
static int FitCheck(int argc, char** argv) {
    const std::string originalText = ReadFile(argv[2]);
    if (originalText.empty()) {
        std::fprintf(stderr, "fit check: could not read %s\n", argv[2]);
        return 1;
    }
    KeyValueList merged;
    std::string error;
    if (!ParseKeyValues(originalText.c_str(), merged, error)) {
        std::fprintf(stderr, "fit check: %s does not parse: %s\n", argv[2], error.c_str());
        return 1;
    }
    for (int i = 3; i < argc; ++i) {
        const std::string patchText = ReadFile(argv[i]);
        if (patchText.empty()) continue;
        KeyValueList patch;
        if (!ParseKeyValues(patchText.c_str(), patch, error)) {
            std::fprintf(stderr, "fit check: patch %s does not parse: %s\n", argv[i], error.c_str());
            return 1;
        }
        MergeKeyValues(merged, patch);
    }
    const std::string compact = SerialiseKeyValues(merged, false);
    KeyValueList round;
    if (!ParseKeyValues(compact.c_str(), round, error)) {
        std::fprintf(stderr, "fit check: merged %s does not parse back: %s\n", argv[2], error.c_str());
        return 1;
    }
    const bool fits = compact.size() <= originalText.size();
    std::printf("  %-44s original %7zu  merged %7zu  %s\n",
        argv[2], originalText.size(), compact.size(), fits ? "fits" : "TOO LARGE, would be refused");
    return fits ? 0 : 1;
}

int main(int argc, char** argv) {
    if (argc >= 4 && std::string(argv[1]) == "--fit") return FitCheck(argc, argv);
    // --- scanner and shapes -------------------------------------------------
    KeyValueList simple = Parse("root { a \"1\" b { c 2 } }");
    assert(simple.size() == 1 && simple[0].key == "root" && simple[0].isBlock);
    assert(simple[0].children.size() == 2);
    assert(simple[0].children[0].key == "a" && simple[0].children[0].value == "1");
    assert(simple[0].children[1].isBlock && simple[0].children[1].children[0].value == "2");

    // Comments, CRLF and bare words.
    KeyValueList comments = Parse("// leading\r\nroot\r\n{\r\n\tkey value // trailing\r\n}\r\n");
    assert(comments[0].children.size() == 1 && comments[0].children[0].value == "value");

    // Escape sequences are carried through verbatim, not decoded: an escaped
    // quote does not end the string, and `\n` keeps its backslash so the
    // engine sees exactly what the original file said.
    KeyValueList escaped = Parse("root { text \"say \\\"hi\\\" now\\nagain\" }");
    assert(escaped[0].children[0].value == "say \\\"hi\\\" now\\nagain");
    const std::string escapedText = SerialiseKeyValues(escaped);
    assert(escapedText.find("\\\"hi\\\"") != std::string::npos);
    assert(escapedText.find("\\n") != std::string::npos);
    KeyValueList reparsed = Parse(escapedText.c_str());
    assert(reparsed[0].children[0].value == escaped[0].children[0].value);

    // Malformed input is rejected rather than silently accepted.
    assert(ParseFails("root { a }"));      // key with no value
    assert(ParseFails("root { a 1 "));     // unterminated block
    assert(ParseFails("}"));               // stray close

    // --- platform conditionals ---------------------------------------------
    // The shape that exposed this: a conditional read as an ordinary token
    // shifts every later pair by one and the block ends on a key with no value.
    KeyValueList fonts = Parse(
        "FontFileTable\n{\n"
        "\t\"lucida console\" \"resource/NorthstarMono.ttf\" [$PC]\n"
        "\t\"arial\" \"resource/Lato-Regular.ttf\"\n"
        "\t\"arial bold\" \"resource/Lato-Regular.ttf\"\n}\n");
    const KeyValue* table = FindKeyValue(fonts, "FontFileTable");
    assert(table && table->children.size() == 3);
    assert(table->children[0].condition == "$PC");
    assert(table->children[0].value == "resource/NorthstarMono.ttf");
    assert(table->children[2].key == "arial bold" && table->children[2].condition.empty());

    // Conditionals survive a round trip, including compound ones.
    KeyValueList conds = Parse("root { a 1 [!$JAPANESE && !$TCHINESE] sub [$PC] { b 2 } }");
    const KeyValue* condRoot = FindKeyValue(conds, "root");
    assert(FindKeyValue(condRoot->children, "a")->condition == "!$JAPANESE && !$TCHINESE");
    assert(FindKeyValue(condRoot->children, "sub")->condition == "$PC");
    KeyValueList condsRound = Parse(SerialiseKeyValues(conds).c_str());
    const KeyValue* condRoundRoot = FindKeyValue(condsRound, "root");
    assert(FindKeyValue(condRoundRoot->children, "a")->condition == "!$JAPANESE && !$TCHINESE");
    assert(FindKeyValue(condRoundRoot->children, "sub")->condition == "$PC");
    assert(FindKeyValue(condRoundRoot->children, "sub")->children[0].value == "2");

    // A patch must land on the entry with the matching conditional, not on
    // whichever one happens to come first.
    KeyValueList condBase = Parse("root { font pc.ttf [$PC] font console.vfont [$GAMECONSOLE] }");
    MergeKeyValues(condBase, Parse("root { font custom.vfont [$GAMECONSOLE] }"));
    const KeyValue* condBaseRoot = FindKeyValue(condBase, "root");
    assert(condBaseRoot->children.size() == 2);
    assert(condBaseRoot->children[0].value == "pc.ttf");
    assert(condBaseRoot->children[1].value == "custom.vfont");

    // --- duplicate keys are preserved --------------------------------------
    KeyValueList dupes = Parse("root { lang { a 1 } lang { b 2 } lang { c 3 } }");
    assert(CountKeyValue(dupes[0].children, "lang") == 3);
    KeyValueList dupesRound = Parse(SerialiseKeyValues(dupes).c_str());
    assert(CountKeyValue(dupesRound[0].children, "lang") == 3);

    // --- merge semantics ----------------------------------------------------
    KeyValueList base = Parse("root { keep 1 replace old sub { x 1 y 2 } }");
    KeyValueList patch = Parse("root { replace new sub { y 9 z 3 } added 7 }");
    MergeKeyValues(base, patch);
    const KeyValue* root = FindKeyValue(base, "root");
    assert(root && root->isBlock);
    assert(FindKeyValue(root->children, "keep")->value == "1");     // untouched
    assert(FindKeyValue(root->children, "replace")->value == "new"); // patch wins
    assert(FindKeyValue(root->children, "added")->value == "7");     // appended
    const KeyValue* sub = FindKeyValue(root->children, "sub");
    assert(FindKeyValue(sub->children, "x")->value == "1");          // kept
    assert(FindKeyValue(sub->children, "y")->value == "9");          // overridden
    assert(FindKeyValue(sub->children, "z")->value == "3");          // added

    // Merging must not collapse duplicates the base already had.
    KeyValueList dupBase = Parse("root { lang { a 1 } lang { b 2 } other 1 }");
    KeyValueList dupPatch = Parse("root { other 2 }");
    MergeKeyValues(dupBase, dupPatch);
    assert(CountKeyValue(FindKeyValue(dupBase, "root")->children, "lang") == 2);
    assert(FindKeyValue(FindKeyValue(dupBase, "root")->children, "other")->value == "2");

    // A block replacing a scalar takes the patch wholesale.
    KeyValueList shape = Parse("root { thing scalar }");
    MergeKeyValues(shape, Parse("root { thing { nested 1 } }"));
    assert(FindKeyValue(FindKeyValue(shape, "root")->children, "thing")->isBlock);

    // #base directives round-trip as directives, not as quoted keys.
    KeyValueList based = Parse("#base \"other.txt\"\nroot { a 1 }");
    assert(based.size() == 2 && based[0].key == "#base" && based[0].value == "other.txt");
    const std::string basedText = SerialiseKeyValues(based);
    assert(basedText.compare(0, 6, "#base ") == 0);
    KeyValueList basedRound = Parse(basedText.c_str());
    assert(basedRound[0].key == "#base" && basedRound[0].value == "other.txt");

    std::puts("KeyValues unit tests passed.");

    // --- the real files, when this test is given them -----------------------
    // Optional so the suite still runs on a machine without a game install.
    if (argc < 4) {
        std::puts("KeyValues live-file check skipped (no paths given).");
        return 0;
    }
    const std::string originalText = ReadFile(argv[1]);
    const std::string customText = ReadFile(argv[2]);
    const std::string serversText = ReadFile(argv[3]);
    if (originalText.empty() || customText.empty() || serversText.empty()) {
        std::fprintf(stderr, "live-file check: could not read all inputs\n");
        return 1;
    }

    KeyValueList merged = Parse(originalText.c_str());
    const KeyValue* playlists = FindKeyValue(merged, "playlists");
    assert(playlists && playlists->isBlock);
    const KeyValue* localized = FindKeyValue(playlists->children, "LocalizedStrings");
    const std::size_t langsBefore = localized ? CountKeyValue(localized->children, "lang") : 0;
    const KeyValue* gamemodesBefore = FindKeyValue(playlists->children, "Gamemodes");
    const std::size_t modesBefore = gamemodesBefore ? gamemodesBefore->children.size() : 0;
    assert(langsBefore > 1 && "the shipped playlist has multiple lang blocks");

    // Ascending mod priority, exactly as the runtime applies them: the last
    // merge wins, so the highest-priority mod (Northstar.Custom) goes last.
    MergeKeyValues(merged, Parse(serversText.c_str()));
    MergeKeyValues(merged, Parse(customText.c_str()));

    playlists = FindKeyValue(merged, "playlists");
    const KeyValue* gamemodes = FindKeyValue(playlists->children, "Gamemodes");
    assert(gamemodes && gamemodes->isBlock);
    for (const char* mode : {"fastball", "gg", "arena", "chamber", "hidden", "inf", "tt", "kr"}) {
        if (!FindKeyValue(gamemodes->children, mode)) {
            std::fprintf(stderr, "live-file check: custom gamemode '%s' missing after merge\n", mode);
            return 1;
        }
    }
    // Stock modes must survive the merge.
    for (const char* mode : {"aitdm", "tdm", "ctf", "lts"}) {
        if (!FindKeyValue(gamemodes->children, mode)) {
            std::fprintf(stderr, "live-file check: stock gamemode '%s' lost in merge\n", mode);
            return 1;
        }
    }
    localized = FindKeyValue(playlists->children, "LocalizedStrings");
    const std::size_t langsAfter = localized ? CountKeyValue(localized->children, "lang") : 0;
    if (langsAfter != langsBefore) {
        std::fprintf(stderr, "live-file check: lang blocks %zu -> %zu, duplicates were collapsed\n",
            langsBefore, langsAfter);
        return 1;
    }

    // Every escape sequence in the original must still be there afterwards.
    // Losing these silently corrupts 2525 localised strings into one long run
    // of text with no line breaks, which no structural check would catch.
    const std::string text = SerialiseKeyValues(merged);
    for (const char* sequence : {"\\n", "\\\""}) {
        const std::size_t before = CountOccurrences(originalText, sequence);
        const std::size_t after = CountOccurrences(text, sequence);
        if (before == 0 || after != before) {
            std::fprintf(stderr, "live-file check: escape '%s' count %zu -> %zu\n",
                sequence, before, after);
            return 1;
        }
    }

    // The serialised result must parse back to the same shape.
    KeyValueList round = Parse(text.c_str());
    const KeyValue* roundPlaylists = FindKeyValue(round, "playlists");
    const KeyValue* roundModes = FindKeyValue(roundPlaylists->children, "Gamemodes");
    if (!roundModes || roundModes->children.size() != gamemodes->children.size()) {
        std::fprintf(stderr, "live-file check: round trip changed the gamemode count\n");
        return 1;
    }
    // The runtime serves the compact form, and the engine reads a served file
    // short if it is longer than the original it measured, so this is a hard
    // constraint rather than a nicety. If a future patch pushes it over, the
    // runtime falls back to vanilla and the custom gamemodes quietly vanish -
    // catch that here instead.
    const std::string compact = SerialiseKeyValues(merged, false);
    if (compact.size() > originalText.size()) {
        std::fprintf(stderr, "live-file check: compact merge %zu bytes exceeds original %zu, "
            "the runtime would refuse to serve it\n", compact.size(), originalText.size());
        return 1;
    }
    KeyValueList compactRound = Parse(compact.c_str());
    const KeyValue* compactModes =
        FindKeyValue(FindKeyValue(compactRound, "playlists")->children, "Gamemodes");
    if (!compactModes || compactModes->children.size() != gamemodes->children.size()) {
        std::fprintf(stderr, "live-file check: compact form lost gamemodes\n");
        return 1;
    }
    std::printf("KeyValues live-file check passed: gamemodes %zu -> %zu, lang blocks %zu, "
        "indented %zu bytes, compact %zu of %zu allowed.\n",
        modesBefore, gamemodes->children.size(), langsAfter, text.size(),
        compact.size(), originalText.size());
    return 0;
}
