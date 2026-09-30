#include "northstar_ps4/pdef_diff.h"

#include <cassert>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>

using namespace northstar::ps4::mods;

int main(int argc, char** argv) {
    std::string base =
        "$ENUM_START modes\n\tone\n$ENUM_END\n"
        "$STRUCT_START item\n\tint count\n\tbool flags[modes]\n$STRUCT_END\n"
        "item items[2]\nstring{8} title\n";
    std::string error;
    assert(ApplyPdiff(base,
        "$ENUM_ADD modes\n\ttwo\n$ENUM_END\n$PROP_START\nfloat score\n", "Example.Mod", error));
    assert(base.find("\ttwo\n$ENUM_END") != std::string::npos);
    assert(base.find("// $PROP_START Example.Mod\nfloat score") != std::string::npos);
    std::size_t bytes = 0;
    assert(PersistenceDefinitionSize(base, bytes, error));
    assert(bytes == 24); // two (int + two bools), eight-char string, float

    std::string unchanged = base;
    assert(!ApplyPdiff(unchanged, "$ENUM_ADD missing\nvalue\n$ENUM_END\n", "Bad", error));
    assert(unchanged == base);
    assert(error.find("does not exist") != std::string::npos);
    PdiffParts parts;
    assert(!ParsePdiff("$ENUM_ADD modes\nvalue\n", parts, error));
    assert(!ParsePdiff("not a directive\n", parts, error));

    if (argc > 1) {
        std::ifstream input(argv[1], std::ios::binary);
        assert(input);
        std::string shipped((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        assert(PersistenceDefinitionSize(shipped, bytes, error));
        assert(bytes == 56350);
        const std::string original = shipped;
        assert(ApplyPdiff(shipped, "$PROP_START\nint ps4PdiffFixture\n", "Fixture", error));
        assert(PersistenceDefinitionSize(shipped, bytes, error));
        assert(bytes == 56354);
        assert(shipped.size() > original.size());
        std::printf("pdef live-file check passed: %zu -> %zu data bytes\n",
            static_cast<std::size_t>(56350), bytes);
    }

    std::puts("pdef_diff tests passed");
    return 0;
}
