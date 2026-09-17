#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

// Portable policy for Northstar's Safe I/O ("save file") API. The rules match
// PC Northstar's mods/modsavefiles.cpp: every mod writes only inside its own
// folder under the profile save root, names are restricted to ASCII, and only
// a small extension whitelist may be created. The PS4 runtime applies these
// before touching the filesystem; the host tests exercise them directly.
namespace northstar::ps4::mods {
// PC default (-maxfoldersize overrides it there; PS4 has no command line).
constexpr std::uint64_t kMaxSaveFolderSize = 52428800; // 50 MiB
constexpr std::size_t kMaxSaveRelativePath = 192;

// PC whitelists the extensions a mod may create. Loading, deleting and listing
// are not restricted, matching PC, where only the write path checks.
inline bool SaveExtensionAllowed(const char* relative) noexcept {
    if (!relative) return false;
    const char* dot = nullptr;
    for (const char* p = relative; *p; ++p) {
        if (*p == '/') dot = nullptr;
        else if (*p == '.') dot = p;
    }
    if (!dot || !dot[1]) return false;
    return std::strcmp(dot, ".txt") == 0 || std::strcmp(dot, ".json") == 0;
}

// True when `relative` stays inside the mod's own save folder. PC resolves the
// path and checks containment; without weakly_canonical on PS4 the equivalent
// is to reject every escape at the syntax level, which is stricter and cannot
// be confused by symlinks. Paths are ASCII-only, exactly as PC documents.
inline bool SavePathSafe(const char* relative, bool allowEmpty = false) noexcept {
    if (!relative) return false;
    if (!*relative) return allowEmpty;
    const std::size_t length = std::strlen(relative);
    if (length >= kMaxSaveRelativePath) return false;
    for (const char* p = relative; *p; ++p) {
        const unsigned char c = static_cast<unsigned char>(*p);
        if (c < 32 || c > 126) return false;
        if (c == '\\' || c == ':') return false;
    }
    if (relative[0] == '/') return false;
    const char* segment = relative;
    while (*segment) {
        const char* end = std::strchr(segment, '/');
        const std::size_t span = end ? static_cast<std::size_t>(end - segment) : std::strlen(segment);
        if (span == 0) return false;
        if (span == 1 && segment[0] == '.') return false;
        if (span == 2 && segment[0] == '.' && segment[1] == '.') return false;
        if (!end) break;
        segment = end + 1;
    }
    return true;
}

// PC refuses to write contents containing NUL, so a mod cannot smuggle a
// second extension past the whitelist check.
inline bool SaveContentsValid(const char* data, std::size_t size) noexcept {
    if (!data) return false;
    for (std::size_t i = 0; i < size; ++i) if (data[i] == '\0') return false;
    return true;
}

// PC keys each mod's folder on the mod directory name, not the display name,
// so two mods sharing a Name still get separate storage.
inline bool SaveFolderNameSafe(const char* folder) noexcept {
    if (!folder || !*folder) return false;
    if (std::strcmp(folder, ".") == 0 || std::strcmp(folder, "..") == 0) return false;
    for (const char* p = folder; *p; ++p) {
        const unsigned char c = static_cast<unsigned char>(*p);
        if (c < 32 || c > 126) return false;
        if (c == '/' || c == '\\' || c == ':') return false;
    }
    return true;
}
} // namespace northstar::ps4::mods
