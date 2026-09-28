#pragma once
// Mod auto-download ("MAD"): the verified-mods list, SHA-256 and PC's naming
// rules. Mirrors NorthstarLauncher's ModDownloader
// (primedev/mods/autodownload/moddownloader.{h,cpp}). Pure and host-testable;
// the runtime's transport, threads and Squirrel natives live in
// launcher/src/runtime_mod_download.inl.
#include "northstar_ps4/server_list.h"
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace northstar::ps4::mods {

constexpr const char* kVerifiedModsUrl =
    "https://raw.githubusercontent.com/R2Northstar/VerifiedMods/main/verified-mods.json";

// PC's ModDownloader::ModInstallState, in order: scripts compare against these
// numbers (menu_ns_moddownload.nut waits for state >= DONE).
enum ModInstallState : int {
    kModManifestoFetching = 0,
    kModDownloading,
    kModChecksuming,
    kModExtracting,
    kModDone,
    kModAborted,
    kModFailed,
    kModFailedReadingArchive,
    kModFailedWritingToDisk,
    kModFetchingFailed,
    kModCorrupted,
    kModNoDiskSpaceAvailable,
    kModNotFound,
    kModUnknownPlatform,
};

enum class VerifiedModPlatform { Unknown, Thunderstore, ModWorkshop };

struct VerifiedModVersion {
    std::string version, checksum, downloadLink;
    VerifiedModPlatform platform = VerifiedModPlatform::Unknown;
};
struct VerifiedMod {
    std::string name;
    std::vector<VerifiedModVersion> versions;
};

inline VerifiedModPlatform ParseVerifiedModPlatform(const std::string& value) {
    if (value == "thunderstore") return VerifiedModPlatform::Thunderstore;
    if (value == "modworkshop") return VerifiedModPlatform::ModWorkshop;
    return VerifiedModPlatform::Unknown;
}

// {"<mod name>": {"Repository": ..., "Versions": [{"Version", "Checksum",
// "DownloadLink", "Platform"}, ...]}, ...}. Like PC, one malformed entry
// rejects the whole list rather than loading part of it.
inline bool ParseVerifiedMods(const char* json, std::vector<VerifiedMod>& out) {
    out.clear();
    const char* p = JsonSkipWs(json);
    if (*p != '{') return false;
    p = JsonSkipWs(p + 1);
    while (*p != '}') {
        VerifiedMod mod;
        if (!DecodeJsonString(p, mod.name)) return false;
        p = JsonSkipWs(JsonSkipString(p));
        if (*p != ':') return false;
        const char* value = JsonSkipWs(p + 1);
        if (*value != '{' || !JsonFindMember(value, "Repository")) return false;
        const char* versions = JsonFindMember(value, "Versions");
        if (!versions || *JsonSkipWs(versions) != '[') return false;
        const char* v = JsonSkipWs(JsonSkipWs(versions) + 1);
        while (*v != ']') {
            if (*v != '{') return false;
            VerifiedModVersion version;
            std::string platform;
            if (!DecodeJsonString(JsonFindMember(v, "Version"), version.version) ||
                !DecodeJsonString(JsonFindMember(v, "Checksum"), version.checksum) ||
                !DecodeJsonString(JsonFindMember(v, "DownloadLink"), version.downloadLink) ||
                !DecodeJsonString(JsonFindMember(v, "Platform"), platform))
                return false;
            version.platform = ParseVerifiedModPlatform(platform);
            mod.versions.push_back(std::move(version));
            v = JsonSkipWs(JsonSkipValue(v));
            if (*v == ',') v = JsonSkipWs(v + 1);
            else if (*v != ']') return false;
        }
        out.push_back(std::move(mod));
        p = JsonSkipWs(JsonSkipValue(value));
        if (*p == ',') p = JsonSkipWs(p + 1);
        else if (*p != '}') return false;
    }
    return true;
}

inline const VerifiedModVersion* FindVerifiedModVersion(const std::vector<VerifiedMod>& mods,
    const std::string& name, const std::string& version) {
    for (const auto& mod : mods) {
        if (mod.name != name) continue;
        for (const auto& v : mod.versions) if (v.version == version) return &v;
    }
    return nullptr;
}

// PC's GetModArchiveName: the URL's last path segment, or for ModWorkshop the
// part after "?filename=".
inline std::string ModArchiveName(const std::string& url) {
    std::string name = url;
    const std::size_t slash = name.find_last_of('/');
    if (slash != std::string::npos) name = name.substr(slash + 1);
    const std::size_t query = name.find('?');
    if (query == std::string::npos) return name;
    const std::size_t equals = name.find('=', query);
    return equals == std::string::npos ? name.substr(0, query) : name.substr(equals + 1);
}

// PC's install folder under the remote mods root: the archive name minus
// ".zip" for Thunderstore, "<archive stem>-<version>" for ModWorkshop. Empty
// when the result would not be a single safe folder name.
inline std::string ModInstallFolder(const std::string& archiveName, const std::string& version,
    VerifiedModPlatform platform) {
    std::string folder;
    if (platform == VerifiedModPlatform::ModWorkshop) {
        const std::size_t dot = archiveName.find_last_of('.');
        folder = (dot == std::string::npos || dot == 0 ? archiveName : archiveName.substr(0, dot)) + "-" + version;
    } else {
        if (archiveName.size() <= 4 || archiveName.compare(archiveName.size() - 4, 4, ".zip") != 0) return {};
        folder = archiveName.substr(0, archiveName.size() - 4);
    }
    if (folder.empty() || folder[0] == '.' || folder.find_first_of("/\\:\r\n\"") != std::string::npos) return {};
    return folder;
}

class Sha256 {
public:
    Sha256() { Reset(); }
    void Reset() {
        static constexpr std::uint32_t kInit[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
        std::memcpy(state_, kInit, sizeof(state_));
        length_ = 0;
        used_ = 0;
    }
    void Update(const void* data, std::size_t size) {
        const auto* bytes = static_cast<const std::uint8_t*>(data);
        length_ += size;
        while (size) {
            const std::size_t take = size < 64 - used_ ? size : 64 - used_;
            std::memcpy(block_ + used_, bytes, take);
            used_ += take;
            bytes += take;
            size -= take;
            if (used_ == 64) { Compress(block_); used_ = 0; }
        }
    }
    // Lowercase hex, the form the verified-mods list uses.
    std::string FinishHex() {
        const std::uint64_t bits = length_ * 8;
        const std::uint8_t pad = 0x80;
        Update(&pad, 1);
        const std::uint8_t zero = 0;
        while (used_ != 56) Update(&zero, 1);
        std::uint8_t tail[8];
        for (int i = 0; i < 8; ++i) tail[i] = static_cast<std::uint8_t>(bits >> (56 - 8 * i));
        Update(tail, 8);
        static constexpr char kHex[] = "0123456789abcdef";
        std::string out;
        for (std::uint32_t word : state_)
            for (int shift = 28; shift >= 0; shift -= 4) out += kHex[(word >> shift) & 0xf];
        return out;
    }

private:
    static std::uint32_t Rotr(std::uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
    void Compress(const std::uint8_t* block) {
        static constexpr std::uint32_t k[64] = {
            0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
            0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
            0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
            0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
            0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
            0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
            0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
            0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
        std::uint32_t w[64];
        for (int i = 0; i < 16; ++i)
            w[i] = (std::uint32_t(block[4 * i]) << 24) | (std::uint32_t(block[4 * i + 1]) << 16) |
                (std::uint32_t(block[4 * i + 2]) << 8) | std::uint32_t(block[4 * i + 3]);
        for (int i = 16; i < 64; ++i) {
            const std::uint32_t s0 = Rotr(w[i - 15], 7) ^ Rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const std::uint32_t s1 = Rotr(w[i - 2], 17) ^ Rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        std::uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3];
        std::uint32_t e = state_[4], f = state_[5], g = state_[6], h = state_[7];
        for (int i = 0; i < 64; ++i) {
            const std::uint32_t t1 = h + (Rotr(e, 6) ^ Rotr(e, 11) ^ Rotr(e, 25)) + ((e & f) ^ (~e & g)) + k[i] + w[i];
            const std::uint32_t t2 = (Rotr(a, 2) ^ Rotr(a, 13) ^ Rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
            h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        state_[0] += a; state_[1] += b; state_[2] += c; state_[3] += d;
        state_[4] += e; state_[5] += f; state_[6] += g; state_[7] += h;
    }

    std::uint32_t state_[8];
    std::uint8_t block_[64];
    std::size_t used_ = 0;
    std::uint64_t length_ = 0;
};

} // namespace northstar::ps4::mods
