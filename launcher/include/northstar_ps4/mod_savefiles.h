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
// PC default; -maxfoldersize in ns_startup_args.txt overrides it, as on PC
// (MaxSaveFolderSize in runtime.cpp).
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

// True when `relative` stays inside the mod's own save folder. PC resolves
// the path (weakly_canonical) and checks it is still under the folder, so
// "./a.txt", "a//b.txt" and "a/../b.txt" are fine and "../a.txt" is not. The
// same is decided here by walking the segments: "." and empty segments stay
// put, ".." climbs one level, and climbing above the folder is refused.
// Paths are ASCII-only, as on PC. Absolute paths are refused, and so are
// backslashes and colons (path separators or drive letters on PC's Windows,
// ordinary characters here).
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
    int depth = 0;
    const char* segment = relative;
    while (*segment) {
        const char* end = std::strchr(segment, '/');
        const std::size_t span = end ? static_cast<std::size_t>(end - segment) : std::strlen(segment);
        if (span == 2 && segment[0] == '.' && segment[1] == '.') {
            if (--depth < 0) return false;
        } else if (span != 0 && !(span == 1 && segment[0] == '.')) {
            ++depth;
        }
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
