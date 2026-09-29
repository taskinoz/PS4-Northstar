#pragma once
#include "json_text.h"
#include "mod_catalog.h"
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// Northstar mod rpaks: `<mod>/paks/*.rpak` plus a `paks/rpak.json` saying when
// each one loads.
//
// PC's rules, from ModManager's rpak block and PakLoadManager:
//   - `Preload[<pak>] == true`  -> load it early, before the engine's own pak.
//   - else `Postload[<pak>]`    -> a vanilla pak name; load ours right after
//                                  that one loads.
//   - else `<pak>: "<regex>"`   -> legacy map-name regex, deprecated upstream.
// Only one method applies per pak, in that order, so a pak listed under both
// Preload and Postload preloads and the Postload entry is ignored.
//
// Mod rpaks are texture and material archives in practice: both of the ones
// Northstar.Custom ships hold only `txtr` and `matl` assets and no models.
namespace northstar::ps4::mods {

enum class RpakLoadKind { None, Preload, Postload };

struct RpakRule {
    std::string pak;    // file name, e.g. "northstarEventModels.rpak"
    std::string after;  // the vanilla pak it follows, when Postload
    RpakLoadKind kind = RpakLoadKind::None;
};

// PC parses rpak.json with RapidJSON's comment and trailing-comma flags. The
// strict parser here would reject both, so they are blanked first - outside
// strings, so a comma or a `//` inside a value is left alone.
inline std::string RelaxJsonToStrict(const char* config) {
    std::string text;
    if (!config) return text;
    for (const char* p = config; *p;) {
        if (*p == '"') {
            const char* end = JsonSkipString(p);
            if (!end || end == p) { text += *p++; continue; }
            text.append(p, end);
            p = end;
        } else if (p[0] == '/' && p[1] == '/') {
            while (*p && *p != '\n') ++p;
            text += ' ';
        } else if (p[0] == '/' && p[1] == '*') {
            p += 2;
            while (*p && !(p[0] == '*' && p[1] == '/')) ++p;
            if (!*p) break;
            p += 2;
            text += ' ';
        } else {
            text += *p++;
        }
    }
    for (std::size_t i = 0; i < text.size();) {
        if (text[i] == '"') {
            const char* end = JsonSkipString(text.c_str() + i);
            if (!end) break;
            i = static_cast<std::size_t>(end - text.c_str());
            continue;
        }
        if (text[i] == ',') {
            const char next = *JsonTextSkipWs(text.c_str() + i + 1);
            if (next == '}' || next == ']') text[i] = ' ';
        }
        ++i;
    }
    return text;
}

// Collects the Preload and Postload members. Anything else in the file, such
// as Aliases or a legacy regex entry, is skipped rather than treated as an
// error, matching PC's member-at-a-time reading.
struct RpakConfigVisitor {
    int depth = 0;
    int section = 0;  // 1 = Preload, 2 = Postload
    std::string pending;
    std::vector<std::pair<std::string, bool>> preload;
    std::vector<std::pair<std::string, std::string>> postload;

    bool OnObjectBegin() { ++depth; return true; }
    bool OnObjectEnd() {
        --depth;
        if (depth <= 1) section = 0;
        pending.clear();
        return true;
    }
    bool OnArrayBegin() { ++depth; return true; }
    bool OnArrayEnd() { --depth; return true; }
    bool OnKey(const char* key, std::size_t size) {
        const std::string name(key, size);
        if (depth == 1) {
            section = name == "Preload" ? 1 : name == "Postload" ? 2 : 0;
            pending.clear();
        } else if (depth == 2 && section != 0) {
            pending = name;
        }
        return true;
    }
    bool OnBool(bool value) {
        if (depth == 2 && section == 1 && !pending.empty()) preload.emplace_back(pending, value);
        pending.clear();
        return true;
    }
    bool OnString(const char* text, std::size_t size) {
        if (depth == 2 && section == 2 && !pending.empty())
            postload.emplace_back(pending, std::string(text, size));
        pending.clear();
        return true;
    }
    bool OnNull() { pending.clear(); return true; }
    bool OnInteger(int) { return OnNull(); }
    bool OnFloat(float) { return OnNull(); }
};

inline bool IsRpakFileName(const char* name) {
    if (!name || std::strchr(name, '/') || std::strchr(name, '\\') || std::strchr(name, ':')) return false;
    const std::string text(name);
    if (text.find("..") != std::string::npos) return false;
    const std::string suffix(".rpak");
    return text.size() > suffix.size() &&
        !text.compare(text.size() - suffix.size(), suffix.size(), suffix);
}

// Decides how one pak loads. An absent or malformed config leaves every pak
// with no rule, which is PC's behaviour too: it warns and the pak stays
// unloaded rather than being loaded at a guessed time.
inline RpakRule ResolveRpakRule(const std::string& pak, const char* config, bool& configUsable) {
    RpakRule rule;
    rule.pak = pak;
    configUsable = false;
    if (!config) return rule;
    const std::string text = RelaxJsonToStrict(config);
    if (*JsonTextSkipWs(text.c_str()) != '{') return rule;
    RpakConfigVisitor visitor;
    if (!JsonParse(text.c_str(), visitor).ok) return rule;
    configUsable = true;
    for (const auto& entry : visitor.preload) {
        if (entry.first == pak && entry.second) {
            rule.kind = RpakLoadKind::Preload;
            return rule;
        }
    }
    for (const auto& entry : visitor.postload) {
        if (entry.first == pak && !entry.second.empty()) {
            rule.kind = RpakLoadKind::Postload;
            rule.after = entry.second;
            return rule;
        }
    }
    return rule;
}

// The pak worker has a `/app0/r2/paks/PS4/%s` format string, but it is not
// applied on the path a load request actually takes: a relative name went to
// the filesystem verbatim and the open failed on the literal
// `../../../R2Northstar/...`. An absolute path is opened as given, so mod paks
// are requested by their full path, under the mod's directory.
inline std::string ModRpakRequestPath(const std::string& modDirectory, const std::string& pak) {
    return modDirectory + "/paks/" + pak;
}

// The engine asks for paks by bare name, but compare on the basename so a
// request carrying a directory still matches.
inline bool RpakNameMatches(const std::string& expected, const char* requested) {
    if (!requested || expected.empty()) return false;
    const char* base = requested;
    for (const char* p = requested; *p; ++p)
        if (*p == '/' || *p == '\\') base = p + 1;
    return expected == base;
}

// RPak v7 keeps its NUL-separated streamed-pak names immediately after the
// 0x58-byte header. Northstar records these at discovery time because the
// later stream open does not identify which RPak caused it.
inline bool ParseRpakStarpakReferences(const void* bytes, std::size_t size,
    std::vector<std::string>& references) {
    references.clear();
    if (!bytes || size < 0x58) return false;
    const auto* data = static_cast<const std::uint8_t*>(bytes);
    if (std::memcmp(data, "RPak", 4) != 0 || data[4] != 7) return false;
    const std::size_t referenceSize =
        static_cast<std::size_t>(data[0x38]) |
        (static_cast<std::size_t>(data[0x39]) << 8);
    if (referenceSize > size - 0x58) return false;
    std::size_t begin = 0;
    for (std::size_t i = 0; i < referenceSize; ++i) {
        if (data[0x58 + i] != 0) continue;
        if (i > begin) {
            const std::string path(reinterpret_cast<const char*>(data + 0x58 + begin), i - begin);
            // Stream paths are relative to r2. Refuse absolute paths and
            // traversal before they can become a filesystem redirect.
            if (path[0] == '/' || path[0] == '\\' || path.find(':') != std::string::npos ||
                path.find("..") != std::string::npos) return false;
            references.push_back(path);
        }
        begin = i + 1;
    }
    return begin == referenceSize;
}

struct RpakTexturePlatforms {
    std::size_t textures = 0;
    std::size_t pc = 0;
    std::size_t ps4 = 0;
    std::size_t other = 0;
};

inline std::uint16_t RpakReadU16(const std::uint8_t* data) {
    return static_cast<std::uint16_t>(data[0]) |
        (static_cast<std::uint16_t>(data[1]) << 8);
}

inline std::uint32_t RpakReadU32(const std::uint8_t* data) {
    return static_cast<std::uint32_t>(data[0]) |
        (static_cast<std::uint32_t>(data[1]) << 8) |
        (static_cast<std::uint32_t>(data[2]) << 16) |
        (static_cast<std::uint32_t>(data[3]) << 24);
}

inline bool RpakAdvance(std::size_t& cursor, std::size_t count,
    std::size_t stride, std::size_t size) {
    if (cursor > size) return false;
    if (stride && count > (size - cursor) / stride) return false;
    cursor += count * stride;
    return true;
}

// Inspect an uncompressed Titanfall 2 v7 archive without following any of its
// pointers outside the supplied buffer. Texture header byte +0x1c is the
// platform/layout marker written by RePak: 0 for PC linear blocks and 8 for
// PS4 Morton-swizzled blocks. Compressed and patch archives are deliberately
// refused; they need decoding/base-pak resolution before page pointers mean
// file offsets.
inline bool InspectRpakTexturePlatforms(const void* bytes, std::size_t size,
    RpakTexturePlatforms& result) {
    result = {};
    constexpr std::size_t kHeaderSize = 0x58;
    constexpr std::size_t kSlabSize = 16;
    constexpr std::size_t kPageSize = 12;
    constexpr std::size_t kPointerSize = 8;
    constexpr std::size_t kAssetSize = 72;
    constexpr std::uint32_t kTextureType =
        static_cast<std::uint32_t>('t') |
        (static_cast<std::uint32_t>('x') << 8) |
        (static_cast<std::uint32_t>('t') << 16) |
        (static_cast<std::uint32_t>('r') << 24);
    if (!bytes || size < kHeaderSize) return false;
    const auto* data = static_cast<const std::uint8_t*>(bytes);
    if (std::memcmp(data, "RPak", 4) != 0 || RpakReadU16(data + 4) != 7 ||
        RpakReadU16(data + 6) != 0 || RpakReadU16(data + 0x3e) != 0) return false;

    const std::size_t pathSize = RpakReadU16(data + 0x38);
    const std::size_t slabCount = RpakReadU16(data + 0x3a);
    const std::size_t pageCount = RpakReadU16(data + 0x3c);
    const std::size_t pointerCount = RpakReadU32(data + 0x40);
    const std::size_t assetCount = RpakReadU32(data + 0x44);
    const std::size_t usesCount = RpakReadU32(data + 0x48);
    const std::size_t dependentsCount = RpakReadU32(data + 0x4c);
    std::size_t cursor = kHeaderSize;
    if (!RpakAdvance(cursor, pathSize, 1, size) ||
        !RpakAdvance(cursor, slabCount, kSlabSize, size)) return false;

    const std::size_t pagesOffset = cursor;
    if (!RpakAdvance(cursor, pageCount, kPageSize, size) ||
        !RpakAdvance(cursor, pointerCount, kPointerSize, size)) return false;
    const std::size_t assetsOffset = cursor;
    if (!RpakAdvance(cursor, assetCount, kAssetSize, size) ||
        !RpakAdvance(cursor, usesCount, kPointerSize, size) ||
        !RpakAdvance(cursor, dependentsCount, 4, size)) return false;
    const std::size_t pageDataOffset = cursor;

    std::vector<std::size_t> pageOffsets;
    pageOffsets.reserve(pageCount);
    std::size_t pageCursor = pageDataOffset;
    for (std::size_t i = 0; i < pageCount; ++i) {
        const std::size_t pageHeader = pagesOffset + i * kPageSize;
        const std::size_t pageBytes = RpakReadU32(data + pageHeader + 8);
        pageOffsets.push_back(pageCursor);
        if (!RpakAdvance(pageCursor, pageBytes, 1, size)) return false;
    }

    for (std::size_t i = 0; i < assetCount; ++i) {
        const std::size_t asset = assetsOffset + i * kAssetSize;
        if (RpakReadU32(data + asset + 68) != kTextureType) continue;
        const std::size_t page = RpakReadU32(data + asset + 16);
        const std::size_t offset = RpakReadU32(data + asset + 20);
        const std::size_t headSize = RpakReadU32(data + asset + 60);
        if (page >= pageCount || headSize <= 0x1c) return false;
        const std::size_t pageBytes = RpakReadU32(data + pagesOffset + page * kPageSize + 8);
        if (offset > pageBytes || headSize > pageBytes - offset) return false;
        const std::size_t header = pageOffsets[page] + offset;
        if (header > size || headSize > size - header) return false;
        const std::uint8_t platform = data[header + 0x1c];
        ++result.textures;
        if (platform == 0) ++result.pc;
        else if (platform == 8) ++result.ps4;
        else ++result.other;
    }
    return true;
}

inline std::string NormaliseRpakStreamPath(const char* path) {
    std::string result;
    if (!path) return result;
    for (const char* p = path; *p; ++p) {
        char c = *p == '\\' ? '/' : *p;
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        result += c;
    }
    constexpr const char* appPrefix = "/app0/r2/";
    constexpr const char* r2Prefix = "r2/";
    if (result.compare(0, std::strlen(appPrefix), appPrefix) == 0)
        result.erase(0, std::strlen(appPrefix));
    else if (result.compare(0, std::strlen(r2Prefix), r2Prefix) == 0)
        result.erase(0, std::strlen(r2Prefix));
    while (!result.empty() && result[0] == '/') result.erase(0, 1);
    return result;
}

inline bool RpakStreamPathMatches(const std::string& embedded, const char* requested) {
    return !embedded.empty() && NormaliseRpakStreamPath(embedded.c_str()) ==
        NormaliseRpakStreamPath(requested);
}
}  // namespace northstar::ps4::mods
