#include "northstar_ps4/regex_lite.h"
#include <cstdio>
#include <cstdlib>
#include <regex>
using northstar::ps4::regexlite::Regex;

static void Check(bool ok, const char* what, int line) {
    if (!ok) {
        std::fprintf(stderr, "line %d: %s\n", line, what);
        std::exit(1);
    }
}
#define CHECK(x) Check((x), #x, __LINE__)

static bool Matches(const char* pattern, const char* text) {
    Regex regex;
    std::string error;
    Check(regex.Compile(pattern, error), pattern, __LINE__);
    return regex.Search(text);
}

static bool Rejects(const char* pattern) {
    Regex regex;
    std::string error;
    return !regex.Compile(pattern, error) && !error.empty();
}

int main() {
    // Against std::regex (ECMAScript) on the host, which has exceptions.
    const char* patterns[] = {"^diag_", "diag_.*_callout$", "^pilot_(grapple|wallrun)_.+", "a+b*c?", "[a-c]{2,3}x",
        "[^_]+_[0-9]{2}$", "\\d\\d", "\\w+\\s\\w+", "^(?:ab|cd)+$", "x{2,}", "colou?r", "^$", "a|", "(a|b)*c",
        "[\\]a]", "[-a]", "a.c", "Weapon_Kraber_Fire_1P", "^weapon_.*_(fire|reload)_[0-9]p$"};
    const char* texts[] = {"diag_sp_intro", "diag_mp_marvin_callout", "pilot_grapple_fire", "pilot_jump", "abbbc",
        "ac", "bcx", "aabcx", "abc_12", "abc_1", "x99", "hello world", "abcd", "abab", "xx", "x", "color",
        "colour", "", "c", "ababc", "]", "-", "abc", "a\nc", "Weapon_Kraber_Fire_1P", "weapon_kraber_fire_1p",
        "weapon_r97_reload_3p", "weapon_r97_reload_3px"};
    for (const char* pattern : patterns) {
        std::regex reference(pattern);
        for (const char* text : texts) {
            const bool expected = std::regex_search(text, reference);
            if (Matches(pattern, text) != expected) {
                std::fprintf(stderr, "pattern %s text %s expected %d\n", pattern, text, expected);
                return 1;
            }
        }
    }

    CHECK(Rejects("(abc"));
    CHECK(Rejects("abc)"));
    CHECK(Rejects("[abc"));
    CHECK(Rejects("*a"));
    CHECK(Rejects("a**"));
    CHECK(Rejects("a{3,1}"));
    CHECK(Rejects("\\q"));
    CHECK(Rejects("(?=a)"));
    CHECK(Rejects("[z-a]"));
    CHECK(Rejects("a\\"));
    CHECK(Rejects("^*"));

    // A pathological pattern stops instead of running away.
    std::string longText(40, 'a');
    Regex slow;
    std::string error;
    CHECK(slow.Compile("(a*)*b", error));
    CHECK(!slow.Search(longText.c_str()));

    std::puts("regex_lite tests passed");
    return 0;
}
