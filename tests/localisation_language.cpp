#include "northstar_ps4/localisation_language.h"

#include <cassert>
#include <cstring>
#include <cstdio>

using northstar::ps4::mods::LocalisationLanguageForSystem;

int main() {
    assert(!std::strcmp(LocalisationLanguageForSystem(0), "japanese"));
    assert(!std::strcmp(LocalisationLanguageForSystem(1), "english"));
    assert(!std::strcmp(LocalisationLanguageForSystem(18), "english"));
    assert(!std::strcmp(LocalisationLanguageForSystem(22), "french"));
    assert(!std::strcmp(LocalisationLanguageForSystem(17), "portuguese"));
    assert(!std::strcmp(LocalisationLanguageForSystem(20), "mspanish"));
    assert(!std::strcmp(LocalisationLanguageForSystem(10), "tchinese"));
    assert(!std::strcmp(LocalisationLanguageForSystem(29), "english"));
    assert(!std::strcmp(LocalisationLanguageForSystem(-1), "english"));
    std::puts("localisation_language tests passed");
    return 0;
}
