#include "northstar_ps4/json_text.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
using namespace northstar::ps4::mods;

// Records the visitor calls as a flat script so parses can be compared exactly.
struct Recorder {
    std::string log;
    bool OnObjectBegin() { log += "{"; return true; }
    bool OnObjectEnd() { log += "}"; return true; }
    bool OnArrayBegin() { log += "["; return true; }
    bool OnArrayEnd() { log += "]"; return true; }
    bool OnKey(const char* t, std::size_t) { log += "k:"; log += t; log += ";"; return true; }
    bool OnString(const char* t, std::size_t) { log += "s:"; log += t; log += ";"; return true; }
    bool OnInteger(int v) { log += "i:" + std::to_string(v) + ";"; return true; }
    bool OnFloat(float v) { char b[32]; std::snprintf(b, sizeof(b), "f:%.3f;", v); log += b; return true; }
    bool OnBool(bool v) { log += v ? "b:1;" : "b:0;"; return true; }
    bool OnNull() { log += "n;"; return true; }
};

static std::string Parse(const char* text, bool expectOk = true) {
    Recorder r;
    const JsonParseResult result = JsonParse(text, r);
    if (result.ok != expectOk) {
        std::fprintf(stderr, "UNEXPECTED ok=%d for [%s] -> %s\n", result.ok ? 1 : 0, text,
            result.ok ? r.log.c_str() : result.message);
        std::fflush(stderr);
        std::abort();
    }
    return result.ok ? r.log : std::string(result.message);
}

int main() {
    assert(Parse("{}") == "{}");
    assert(Parse(" { \"a\" : 1 , \"b\" : [ true , null ] } ") == "{k:a;i:1;k:b;[b:1;n;]}");
    assert(Parse("{\"n\":-12}") == "{k:n;i:-12;}");
    assert(Parse("{\"f\":1.5}") == "{k:f;f:1.500;}");
    assert(Parse("{\"e\":2e2}") == "{k:e;f:200.000;}");
    // 32-bit Squirrel integers: anything wider stays a float rather than wrapping.
    assert(Parse("{\"big\":99999999999}") == "{k:big;f:99999997952.000;}");
    assert(Parse("{\"s\":\"a\\nb\"}") == "{k:s;s:a\nb;}");
    assert(Parse("{\"s\":\"\\u0041\\u00e9\"}") == "{k:s;s:A\xc3\xa9;}");
    assert(Parse("{\"s\":\"\\ud83d\\ude00\"}") == "{k:s;s:\xf0\x9f\x98\x80;}");
    // Failures report the reason and where parsing stopped, as PC does.
    assert(Parse("{", false) == "expected a string member name");
    assert(Parse("{\"a\" 1}", false) == "expected ':' after a member name");
    assert(Parse("{} junk", false) == "trailing content after the document");
    assert(Parse("[1,]", false) == "expected a value");
    assert(Parse("{\"a\":01}", false) == "numbers may not have leading zeros");
    assert(Parse("{\"a\":\"\\u00\"}", false) == "invalid string");
    assert(Parse("nul", false) == "expected a value");
    {
        Recorder r;
        std::string deep;
        for (int i = 0; i <= kJsonMaxDepth + 2; ++i) deep += "[";
        assert(!JsonParse(deep.c_str(), r).ok);
    }
    {
        Recorder r;
        const char* text = "{\"a\" 1}";
        const JsonParseResult result = JsonParse(text, r);
        assert(!result.ok && result.offset == 5);
    }
    std::string escaped;
    JsonEscapeInto(escaped, "a\"b\\c\nd\x01", 8);
    assert(escaped == "\"a\\\"b\\\\c\\nd\\u0001\"");
    std::string number;
    JsonAppendFloat(number, 1.5f);
    assert(number == "1.5");
    number.clear();
    JsonAppendFloat(number, 1.0f / 0.0f);
    assert(number == "null");
    std::puts("JSON text tests passed.");
}
