#include "northstar_ps4/chat_text.h"
#include <cassert>
#include <cstdio>
#include <string>
using namespace northstar::ps4::chat;

// Records the writer calls as a flat script.
struct Recorder {
    std::string log;
    void Text(const char* text, std::size_t length) { log += "t:" + std::string(text, length) + ";"; }
    void Color(ChatColor c) {
        char b[48];
        std::snprintf(b, sizeof(b), "c:%d,%d,%d,%d;", c.r, c.g, c.b, c.a);
        log += b;
    }
    void Swatch(ChatSwatch s) { log += "s:" + std::to_string(static_cast<int>(s)) + ";"; }
};

static std::string Write(const char* text) {
    Recorder r;
    WriteChatText(text, r);
    return r.log;
}

static std::string Clean(const char* text, bool colors = true) {
    std::string s(text);
    RemoveAsciiControlSequences(s.data(), colors);
    return s;
}

static void Check(const std::string& got, const char* want, int line) {
    if (got != want) {
        std::fprintf(stderr, "line %d: got [%s] want [%s]\n", line, got.c_str(), want);
        std::exit(1);
    }
}
#define CHECK(got, want) Check((got), (want), __LINE__)

int main() {
    // Plain text and the escapes the Northstar client chat script writes.
    CHECK(Write("hello"), "t:hello;");
    CHECK(Write("\x1b[111mname"), "s:1;t:name;");
    CHECK(Write("\x1b[0m"), "s:0;t:;");
    CHECK(Write("\x1b[95mServer"), "c:214,112,214,255;t:Server;");
    CHECK(Write("a\x1b[31mb"), "t:a;c:205,49,49,255;t:b;");
    // Expanded colours.
    CHECK(Write("\x1b[38;2;1;2;3mx"), "c:1,2,3,255;t:x;");
    CHECK(Write("\x1b[38;5;196m"), "c:255,0,0,255;t:;");
    CHECK(Write("\x1b[38;5;232m"), "c:8,8,8,255;t:;");
    CHECK(Write("\x1b[38;5;3m"), "c:229,229,16,255;t:;");
    CHECK(Write("\x1b[1;113mz"), "s:3;t:z;");
    // Malformed: PC prints what follows the bad escape.
    CHECK(Write("\x1b[mtext"), "t:mtext;");
    CHECK(Write("\x1b[31xy"), "c:205,49,49,255;t:xy;");
    CHECK(Write("\x1b[38;2;300;1;1mq"), "t:q;");
    // Text before an escape is cut at 255 bytes, as PC's buffer does.
    std::string longText(300, 'a');
    CHECK(Write((longText + "\x1b[0m").c_str()), ("t:" + std::string(255, 'a') + ";s:0;t:;").c_str());

    // Control characters.
    CHECK(Clean("a\x01" "b\tc"), "a b c");
    CHECK(Clean("line\nnext"), "line\nnext");
    CHECK(Clean("\x1b[31mred"), "\x1b[31mred");
    CHECK(Clean("\x1b[31mred", false), " [31mred");
    // PC's check steps past "\x1b[" before failing, so the byte after that is
    // blanked and the escape itself stays.
    CHECK(Clean("\x1b[31xred"), "\x1b[ 1xred");
    CHECK(Clean("caf\xc3\xa9"), "caf\xc3\xa9");
    CHECK(Clean("bad\xc3" "x"), "bad x");
    CHECK(Clean("\x80lone"), " lone");
    // Custom message type bytes become spaces once read.
    CHECK(Clean("\x02whisper"), " whisper");

    // UTF-8 to UTF-16.
    assert(Utf8ToUtf16("abc", 3) == u"abc");
    assert(Utf8ToUtf16("caf\xc3\xa9", 5) == u"café");
    assert(Utf8ToUtf16("\xe2\x82\xac", 3) == u"€");
    assert(Utf8ToUtf16("\xf0\x9f\x98\x80", 4) == u"\U0001F600");
    assert(Utf8ToUtf16("\xc3", 1) == u"�");
    assert(Utf8ToUtf16("\xff" "a", 2) == u"�a");
    assert(Utf8ToUtf16("\xed\xa0\x80", 3) == u"���");

    // Colours pack as the game reads them: r in the low byte.
    assert((ChatColor{1, 2, 3, 4}.Packed() == 0x04030201u));

    std::puts("chat_text tests passed");
    return 0;
}
