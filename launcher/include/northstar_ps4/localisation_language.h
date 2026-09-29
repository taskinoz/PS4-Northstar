#pragma once

#include <cstdint>

namespace northstar::ps4::mods {

// OrbisSystemParamLanguage values. Keep this portable header independent of
// the PS4 SDK so the mapping is covered by host tests.
inline const char* LocalisationLanguageForSystem(int32_t language) noexcept {
    switch (language) {
    case 0: return "japanese";
    case 1: case 18: return "english";
    case 2: case 22: return "french";
    case 3: return "spanish";
    case 4: return "german";
    case 5: return "italian";
    case 7: case 17: return "portuguese";
    case 8: return "russian";
    case 10: return "tchinese";
    case 16: return "polish";
    case 20: return "mspanish";
    default: return "english";
    }
}

} // namespace northstar::ps4::mods
