#pragma once
// Zip reading for mod auto-download: the central directory, CRC-32 and a
// streaming DEFLATE decoder. PC extracts with minizip
// (primedev/mods/autodownload/moddownloader.cpp, ExtractMod); the toolchain
// has no zlib, so this is a small RFC 1951 decoder in the style of zlib's
// puff.c. Pure and host-testable; the runtime supplies the file reads and
// writes (launcher/src/runtime_mod_download.inl).
//
// Only what verified mods use is supported: stored (0) and deflated (8)
// entries, no encryption, no zip64. Anything else is refused, not guessed at.
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace northstar::ps4::mods {

inline std::uint32_t Crc32Update(std::uint32_t crc, const std::uint8_t* data, std::size_t size) noexcept {
    // Table built on first use; the module's static constructors never run,
    // so it cannot be a constexpr-initialised global with a constructor.
    static std::uint32_t table[256];
    static bool ready = false;
    if (!ready) {
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xedb88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        ready = true;
    }
    crc = ~crc;
    for (std::size_t i = 0; i < size; ++i) crc = table[(crc ^ data[i]) & 0xff] ^ (crc >> 8);
    return ~crc;
}

enum class InflateStatus { Ok, InputEnded, BadData, OutputFailed };

// `Source` provides `int Next()` returning the next compressed byte or -1.
// `Sink` provides `bool Write(const std::uint8_t*, std::size_t)`.
// The decoder is ~100 KiB of state, so callers should heap-allocate it.
template<class Source, class Sink>
class Inflater {
public:
    Inflater(Source& source, Sink& sink) : source_(source), sink_(sink) {}

    InflateStatus Run() {
        int last = 0;
        do {
            last = Bits(1);
            const int type = Bits(2);
            if (failed_) return InflateStatus::InputEnded;
            InflateStatus status = InflateStatus::BadData;
            if (type == 0) status = Stored();
            else if (type == 1) status = Fixed();
            else if (type == 2) status = Dynamic();
            if (status != InflateStatus::Ok) return status;
        } while (!last);
        return Flush() ? InflateStatus::Ok : InflateStatus::OutputFailed;
    }

    std::uint64_t Written() const noexcept { return total_; }
    std::uint32_t Crc() const noexcept { return crc_; }

private:
    struct Huffman {
        std::uint16_t counts[16];
        std::uint16_t symbols[288];
    };

    int Bits(int need) {
        std::uint32_t value = bitBuffer_;
        while (bitCount_ < need) {
            const int next = source_.Next();
            if (next < 0) { failed_ = true; return 0; }
            value |= static_cast<std::uint32_t>(next) << bitCount_;
            bitCount_ += 8;
        }
        bitBuffer_ = value >> need;
        bitCount_ -= need;
        return static_cast<int>(value & ((1u << need) - 1));
    }

    // Lengths of 0 mean "unused". Incomplete codes are accepted (a distance
    // code with one symbol is legal); decoding an unused code fails instead.
    static bool Build(Huffman& h, const std::uint8_t* lengths, int count) {
        std::memset(h.counts, 0, sizeof(h.counts));
        for (int i = 0; i < count; ++i) ++h.counts[lengths[i]];
        if (h.counts[0] == count) return true;
        int left = 1;
        for (int len = 1; len < 16; ++len) {
            left <<= 1;
            left -= h.counts[len];
            if (left < 0) return false;
        }
        std::uint16_t offsets[16];
        offsets[1] = 0;
        for (int len = 1; len < 15; ++len) offsets[len + 1] = offsets[len] + h.counts[len];
        for (int i = 0; i < count; ++i)
            if (lengths[i]) h.symbols[offsets[lengths[i]]++] = static_cast<std::uint16_t>(i);
        return true;
    }

    int Decode(const Huffman& h) {
        int code = 0, first = 0, index = 0;
        for (int len = 1; len < 16; ++len) {
            code |= Bits(1);
            if (failed_) return -1;
            const int count = h.counts[len];
            if (code - count < first) return h.symbols[index + (code - first)];
            index += count;
            first += count;
            first <<= 1;
            code <<= 1;
        }
        return -2;
    }

    bool Put(std::uint8_t byte) {
        window_[windowPos_] = byte;
        windowPos_ = (windowPos_ + 1) & (kWindow - 1);
        chunk_[chunkUsed_++] = byte;
        ++total_;
        return chunkUsed_ < sizeof(chunk_) || Flush();
    }

    bool Flush() {
        if (!chunkUsed_) return true;
        crc_ = Crc32Update(crc_, chunk_, chunkUsed_);
        const bool ok = sink_.Write(chunk_, chunkUsed_);
        chunkUsed_ = 0;
        return ok;
    }

    InflateStatus Stored() {
        bitBuffer_ = 0;
        bitCount_ = 0;
        int header[4];
        for (int& b : header) {
            b = source_.Next();
            if (b < 0) return InflateStatus::InputEnded;
        }
        const unsigned length = static_cast<unsigned>(header[0] | (header[1] << 8));
        const unsigned complement = static_cast<unsigned>(header[2] | (header[3] << 8));
        if (length != (~complement & 0xffffu)) return InflateStatus::BadData;
        for (unsigned i = 0; i < length; ++i) {
            const int b = source_.Next();
            if (b < 0) return InflateStatus::InputEnded;
            if (!Put(static_cast<std::uint8_t>(b))) return InflateStatus::OutputFailed;
        }
        return InflateStatus::Ok;
    }

    InflateStatus Codes(const Huffman& lengths, const Huffman& distances) {
        static constexpr std::uint16_t kLengthBase[29] = {3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
        static constexpr std::uint8_t kLengthExtra[29] = {0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
        static constexpr std::uint16_t kDistBase[30] = {1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
        static constexpr std::uint8_t kDistExtra[30] = {0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};
        for (;;) {
            int symbol = Decode(lengths);
            if (failed_) return InflateStatus::InputEnded;
            if (symbol < 0) return InflateStatus::BadData;
            if (symbol < 256) {
                if (!Put(static_cast<std::uint8_t>(symbol))) return InflateStatus::OutputFailed;
                continue;
            }
            if (symbol == 256) return InflateStatus::Ok;
            symbol -= 257;
            if (symbol >= 29) return InflateStatus::BadData;
            const int length = kLengthBase[symbol] + Bits(kLengthExtra[symbol]);
            const int distSymbol = Decode(distances);
            if (failed_) return InflateStatus::InputEnded;
            if (distSymbol < 0 || distSymbol >= 30) return InflateStatus::BadData;
            const unsigned distance = kDistBase[distSymbol] + static_cast<unsigned>(Bits(kDistExtra[distSymbol]));
            if (failed_) return InflateStatus::InputEnded;
            if (distance > total_ || distance > kWindow) return InflateStatus::BadData;
            for (int i = 0; i < length; ++i) {
                const std::uint8_t byte = window_[(windowPos_ - distance) & (kWindow - 1)];
                if (!Put(byte)) return InflateStatus::OutputFailed;
            }
        }
    }

    InflateStatus Fixed() {
        if (!fixedReady_) {
            std::uint8_t lengths[288 + 30];
            int i = 0;
            for (; i < 144; ++i) lengths[i] = 8;
            for (; i < 256; ++i) lengths[i] = 9;
            for (; i < 280; ++i) lengths[i] = 7;
            for (; i < 288; ++i) lengths[i] = 8;
            for (; i < 288 + 30; ++i) lengths[i] = 5;
            Build(fixedLengths_, lengths, 288);
            Build(fixedDistances_, lengths + 288, 30);
            fixedReady_ = true;
        }
        return Codes(fixedLengths_, fixedDistances_);
    }

    InflateStatus Dynamic() {
        static constexpr std::uint8_t kOrder[19] = {16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
        const int literalCount = Bits(5) + 257;
        const int distanceCount = Bits(5) + 1;
        const int codeCount = Bits(4) + 4;
        if (failed_) return InflateStatus::InputEnded;
        if (literalCount > 286 || distanceCount > 30) return InflateStatus::BadData;
        std::uint8_t lengths[286 + 30]{};
        for (int i = 0; i < codeCount; ++i) lengths[kOrder[i]] = static_cast<std::uint8_t>(Bits(3));
        if (failed_) return InflateStatus::InputEnded;
        Huffman codeLengths;
        if (!Build(codeLengths, lengths, 19)) return InflateStatus::BadData;
        std::memset(lengths, 0, sizeof(lengths));
        int index = 0;
        while (index < literalCount + distanceCount) {
            int symbol = Decode(codeLengths);
            if (failed_) return InflateStatus::InputEnded;
            if (symbol < 0) return InflateStatus::BadData;
            if (symbol < 16) { lengths[index++] = static_cast<std::uint8_t>(symbol); continue; }
            std::uint8_t repeat = 0;
            int times = 0;
            if (symbol == 16) {
                if (index == 0) return InflateStatus::BadData;
                repeat = lengths[index - 1];
                times = 3 + Bits(2);
            } else if (symbol == 17) {
                times = 3 + Bits(3);
            } else {
                times = 11 + Bits(7);
            }
            if (failed_) return InflateStatus::InputEnded;
            if (index + times > literalCount + distanceCount) return InflateStatus::BadData;
            while (times--) lengths[index++] = repeat;
        }
        if (lengths[256] == 0) return InflateStatus::BadData;
        Huffman literals, distances;
        if (!Build(literals, lengths, literalCount) || !Build(distances, lengths + literalCount, distanceCount))
            return InflateStatus::BadData;
        return Codes(literals, distances);
    }

    static constexpr unsigned kWindow = 32768;
    Source& source_;
    Sink& sink_;
    std::uint32_t bitBuffer_ = 0;
    int bitCount_ = 0;
    bool failed_ = false;
    bool fixedReady_ = false;
    Huffman fixedLengths_{}, fixedDistances_{};
    std::uint8_t window_[kWindow]{};
    unsigned windowPos_ = 0;
    std::uint8_t chunk_[64 * 1024]{};
    std::size_t chunkUsed_ = 0;
    std::uint64_t total_ = 0;
    std::uint32_t crc_ = 0;
};

struct ZipEntry {
    std::string name;
    std::uint16_t method = 0;
    std::uint16_t flags = 0;
    std::uint32_t crc = 0;
    std::uint32_t compressedSize = 0;
    std::uint32_t size = 0;
    std::uint32_t localHeaderOffset = 0;
    bool IsDirectory() const { return !name.empty() && name.back() == '/'; }
};

inline std::uint16_t ZipU16(const std::uint8_t* p) noexcept { return static_cast<std::uint16_t>(p[0] | (p[1] << 8)); }
inline std::uint32_t ZipU32(const std::uint8_t* p) noexcept {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
        (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

struct ZipDirectoryLocation { std::uint32_t offset = 0, size = 0; std::uint16_t entries = 0; };

// `tail` is the last `tailSize` bytes of an archive of `archiveSize` bytes
// (up to 64 KiB + 22, enough for the longest comment).
inline bool FindZipDirectory(const std::uint8_t* tail, std::size_t tailSize, std::uint64_t archiveSize,
    ZipDirectoryLocation& out) noexcept {
    if (tailSize < 22) return false;
    for (std::size_t i = tailSize - 22 + 1; i-- > 0;) {
        const std::uint8_t* p = tail + i;
        if (ZipU32(p) != 0x06054b50u) continue;
        if (22 + static_cast<std::size_t>(ZipU16(p + 20)) != tailSize - i) continue;  // comment must end the file
        if (ZipU16(p + 4) != 0 || ZipU16(p + 6) != 0) return false;  // multi-disk
        out.entries = ZipU16(p + 10);
        out.size = ZipU32(p + 12);
        out.offset = ZipU32(p + 16);
        if (out.entries == 0xffff || out.size == 0xffffffffu || out.offset == 0xffffffffu) return false;  // zip64
        return static_cast<std::uint64_t>(out.offset) + out.size <= archiveSize;
    }
    return false;
}

inline bool ParseZipDirectory(const std::uint8_t* data, std::size_t size, std::uint16_t expected,
    std::vector<ZipEntry>& out) {
    out.clear();
    std::size_t at = 0;
    while (out.size() < expected) {
        if (size - at < 46 || ZipU32(data + at) != 0x02014b50u) return false;
        const std::uint8_t* p = data + at;
        ZipEntry entry;
        entry.flags = ZipU16(p + 8);
        entry.method = ZipU16(p + 10);
        entry.crc = ZipU32(p + 16);
        entry.compressedSize = ZipU32(p + 20);
        entry.size = ZipU32(p + 24);
        const std::size_t nameLength = ZipU16(p + 28);
        const std::size_t extra = ZipU16(p + 30) + static_cast<std::size_t>(ZipU16(p + 32));
        entry.localHeaderOffset = ZipU32(p + 42);
        if (size - at - 46 < nameLength + extra) return false;
        entry.name.assign(reinterpret_cast<const char*>(p + 46), nameLength);
        if (entry.compressedSize == 0xffffffffu || entry.size == 0xffffffffu || entry.localHeaderOffset == 0xffffffffu)
            return false;
        out.push_back(std::move(entry));
        at += 46 + nameLength + extra;
    }
    return true;
}

// Offset of an entry's data, from its 30-byte local header.
inline bool ZipDataOffset(const std::uint8_t* localHeader, const ZipEntry& entry, std::uint64_t& out) noexcept {
    if (ZipU32(localHeader) != 0x04034b50u) return false;
    out = static_cast<std::uint64_t>(entry.localHeaderOffset) + 30 + ZipU16(localHeader + 26) + ZipU16(localHeader + 28);
    return true;
}

// An archive path made safe to join under the destination: forward slashes,
// no empty, "." or ".." segments, not absolute, no drive letters. Empty when
// the name cannot be made safe, and the entry is then refused.
inline std::string SanitizeArchivePath(const std::string& name) {
    std::string out;
    std::size_t start = 0;
    std::string normalized = name;
    for (char& c : normalized) if (c == '\\') c = '/';
    if (normalized.empty() || normalized[0] == '/' || normalized.find(':') != std::string::npos) return {};
    while (start <= normalized.size()) {
        std::size_t end = normalized.find('/', start);
        if (end == std::string::npos) end = normalized.size();
        const std::string segment = normalized.substr(start, end - start);
        if (segment == "." || segment == "..") return {};
        if (!segment.empty()) {
            for (unsigned char c : segment) if (c < 0x20) return {};
            if (!out.empty()) out += '/';
            out += segment;
        } else if (end != normalized.size()) {
            // An empty segment in the middle ("a//b") is refused; a trailing
            // slash only marks a directory.
            return {};
        }
        start = end + 1;
    }
    return out;
}

// PC's ModWorkshop rule: the folder holding the first mod.json in archive
// order is the mod root, and paths are taken relative to it. Thunderstore
// archives are extracted whole (root ""), and their mods/<Name> folders are
// found by discovery. Returns false when a ModWorkshop archive has no mod.json.
inline bool FindModWorkshopRoot(const std::vector<ZipEntry>& entries, std::string& root) {
    for (const auto& entry : entries) {
        const std::string path = SanitizeArchivePath(entry.name);
        if (path.empty() || entry.IsDirectory()) continue;
        const std::size_t slash = path.rfind('/');
        const std::string file = slash == std::string::npos ? path : path.substr(slash + 1);
        if (file != "mod.json") continue;
        root = slash == std::string::npos ? std::string() : path.substr(0, slash + 1);
        return true;
    }
    return false;
}

// The destination path of an entry relative to the install folder, or empty
// for entries to skip: directories, unsafe names and, for a ModWorkshop root,
// anything outside it (PC would write those above the install folder).
inline std::string ZipInstallPath(const ZipEntry& entry, const std::string& root) {
    if (entry.IsDirectory()) return {};
    const std::string path = SanitizeArchivePath(entry.name);
    if (path.empty() || path.compare(0, root.size(), root) != 0 || path.size() == root.size()) return {};
    return path.substr(root.size());
}

} // namespace northstar::ps4::mods
