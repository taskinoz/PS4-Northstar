#pragma once

#include "mod_rpaks.h"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <string>
#include <utility>
#include <vector>

// Offline conversion of RePak/Titanfall 2 v7 PC textures to the layout used by
// the PS4 build. This is deliberately separate from the runtime: profiles are
// converted while they are staged, leaving the user's source mods untouched
// and keeping allocation/file rewriting out of the game process.
namespace northstar::ps4::mods {

struct RpakPs4ConversionReport {
    std::size_t textures = 0;
    std::size_t permanentBytesBefore = 0;
    std::size_t permanentBytesAfter = 0;
    std::size_t streamedBlocks = 0;
};

inline void RpakWriteU16(std::uint8_t* data, std::uint16_t value) {
    data[0] = static_cast<std::uint8_t>(value);
    data[1] = static_cast<std::uint8_t>(value >> 8);
}

inline void RpakWriteU32(std::uint8_t* data, std::uint32_t value) {
    for (int i = 0; i < 4; ++i) data[i] = static_cast<std::uint8_t>(value >> (i * 8));
}

inline std::uint64_t RpakReadU64(const std::uint8_t* data) {
    std::uint64_t value = 0;
    for (int i = 0; i < 8; ++i) value |= static_cast<std::uint64_t>(data[i]) << (i * 8);
    return value;
}

inline void RpakWriteU64(std::uint8_t* data, std::uint64_t value) {
    for (int i = 0; i < 8; ++i) data[i] = static_cast<std::uint8_t>(value >> (i * 8));
}

inline std::size_t RpakAlign(std::size_t value, std::size_t alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

// x is bytes per compression block (or pixel); y is the block width/height.
// This is the v8 texture-format table used by Titanfall 2 and RePak.
inline bool RpakTextureFormat(std::uint16_t format, std::size_t& x, std::size_t& y) {
    static constexpr std::uint8_t kX[] = {
        8,8,16,16,16,16,8,8,16,16,16,16,16,16,16,16,16,12,12,12,
        8,8,8,8,8,8,8,8,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,
        2,2,2,2,2,2,2,2,2,1,1,1,1,1,4,4,4,2
    };
    static constexpr std::uint8_t kY[] = {
        4,4,4,4,4,4,4,4,4,4,4,4,4,4,1,1,1,1,1,1,
        1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
        1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1
    };
    static_assert(sizeof(kX) == sizeof(kY));
    if (format >= sizeof(kX)) return false;
    x = kX[format];
    y = kY[format];
    return true;
}

inline std::size_t RpakLinearMipSize(std::size_t width, std::size_t height,
    std::size_t bytesPerBlock, std::size_t blockPixels) {
    const std::size_t blocksX = (width + blockPixels - 1) / blockPixels;
    const std::size_t blocksY = (height + blockPixels - 1) / blockPixels;
    if (blocksX > std::numeric_limits<std::size_t>::max() / blocksY ||
        blocksX * blocksY > std::numeric_limits<std::size_t>::max() / bytesPerBlock) return 0;
    return blocksX * blocksY * bytesPerBlock;
}

inline std::size_t RpakPs4MipSize(std::size_t width, std::size_t height,
    std::size_t bytesPerBlock, std::size_t blockPixels) {
    const std::size_t blocksX = (width + blockPixels - 1) / blockPixels;
    const std::size_t blocksY = (height + blockPixels - 1) / blockPixels;
    const std::size_t tilesX = (blocksX + 7) / 8;
    const std::size_t tilesY = (blocksY + 7) / 8;
    if (tilesX > std::numeric_limits<std::size_t>::max() / tilesY ||
        tilesX * tilesY > std::numeric_limits<std::size_t>::max() / (64 * bytesPerBlock)) return 0;
    return tilesX * tilesY * 64 * bytesPerBlock;
}

inline std::size_t RpakMorton8(std::size_t value) {
    // Deinterleave the six low bits into y:x. This matches LegionPlus's
    // Morton(k, 8, 8), used to unswizzle retail PS4 textures.
    const std::size_t x = (value & 1) | ((value & 4) >> 1) | ((value & 16) >> 2);
    const std::size_t y = ((value & 2) >> 1) | ((value & 8) >> 2) | ((value & 32) >> 3);
    return y * 8 + x;
}

inline bool RpakSwizzleMip(const std::uint8_t* source, std::size_t sourceSize,
    std::size_t width, std::size_t height, std::size_t bytesPerBlock,
    std::size_t blockPixels, std::vector<std::uint8_t>& output) {
    const std::size_t linearSize = RpakLinearMipSize(width, height, bytesPerBlock, blockPixels);
    const std::size_t tiledSize = RpakPs4MipSize(width, height, bytesPerBlock, blockPixels);
    if (!linearSize || !tiledSize || sourceSize < linearSize) return false;
    const std::size_t blocksX = (width + blockPixels - 1) / blockPixels;
    const std::size_t blocksY = (height + blockPixels - 1) / blockPixels;
    const std::size_t tilesX = (blocksX + 7) / 8;
    const std::size_t tilesY = (blocksY + 7) / 8;
    output.assign(tiledSize, 0);
    std::size_t destination = 0;
    for (std::size_t tileY = 0; tileY < tilesY; ++tileY) {
        for (std::size_t tileX = 0; tileX < tilesX; ++tileX) {
            for (std::size_t k = 0; k < 64; ++k) {
                const std::size_t morton = RpakMorton8(k);
                const std::size_t y = tileY * 8 + morton / 8;
                const std::size_t x = tileX * 8 + morton % 8;
                if (x < blocksX && y < blocksY) {
                    const std::size_t sourceOffset = (y * blocksX + x) * bytesPerBlock;
                    std::memcpy(output.data() + destination, source + sourceOffset, bytesPerBlock);
                }
                destination += bytesPerBlock;
            }
        }
    }
    return destination == tiledSize;
}

struct RpakTextureConversionSpec {
    std::size_t descriptor = 0;
    std::uint32_t headPage = 0;
    std::uint32_t headOffset = 0;
    std::uint32_t cpuPage = 0;
    std::uint32_t cpuOffset = 0;
    std::uint16_t width = 0;
    std::uint16_t height = 0;
    std::uint16_t format = 0;
    std::uint8_t permanentMips = 0;
    std::uint8_t streamedMips = 0;
    std::uint8_t arrays = 1;
    std::uint64_t packedStream = ~std::uint64_t(0);
    std::size_t oldPermanentSize = 0;
    std::size_t newTotalSize = 0;
    std::vector<std::uint8_t> permanent;
};

inline bool RpakConvertMipGroup(const std::uint8_t* source, std::size_t available,
    const RpakTextureConversionSpec& texture, std::size_t firstMip, std::size_t mipCount,
    std::vector<std::uint8_t>& output, std::size_t& consumed, std::string& error) {
    output.clear();
    consumed = 0;
    std::size_t bytesPerBlock = 0, blockPixels = 0;
    if (!RpakTextureFormat(texture.format, bytesPerBlock, blockPixels)) {
        error = "unsupported texture format " + std::to_string(texture.format);
        return false;
    }
    std::vector<std::vector<std::uint8_t>> convertedMips(mipCount);
    for (std::size_t reverse = 0; reverse < mipCount; ++reverse) {
        const std::size_t mip = firstMip + mipCount - 1 - reverse;
        const std::size_t width = std::max<std::size_t>(1, texture.width >> mip);
        const std::size_t height = std::max<std::size_t>(1, texture.height >> mip);
        const std::size_t linear = RpakLinearMipSize(width, height, bytesPerBlock, blockPixels);
        const std::size_t aligned = RpakAlign(linear, 16);
        for (std::size_t array = 0; array < texture.arrays; ++array) {
            if (!linear || aligned > available - std::min(available, consumed)) {
                error = "texture mip data is truncated";
                return false;
            }
            std::vector<std::uint8_t> mipBytes;
            if (!RpakSwizzleMip(source + consumed, available - consumed, width, height,
                    bytesPerBlock, blockPixels, mipBytes)) {
                error = "texture mip swizzle failed";
                return false;
            }
            convertedMips[mip - firstMip].insert(convertedMips[mip - firstMip].end(),
                mipBytes.begin(), mipBytes.end());
            consumed += aligned;
        }
    }
    // PC v8 textures are stored bottom-to-top inside each source (smallest
    // mip first). Retail PS4 v8 textures use top-to-bottom sorting whenever
    // swizzle != 0, so reverse the physical mip order while converting.
    for (const auto& mipBytes : convertedMips)
        output.insert(output.end(), mipBytes.begin(), mipBytes.end());
    return true;
}

struct RpakPageReplacement {
    std::size_t start = 0;
    std::size_t oldSize = 0;
    std::vector<std::uint8_t> data;
};

inline bool ConvertRpakTexturesToPs4(std::vector<std::uint8_t>& rpak,
    std::vector<std::vector<std::uint8_t>>& starpaks,
    RpakPs4ConversionReport& report, std::string& error) {
    report = {};
    error.clear();
    constexpr std::size_t kHeaderSize = 0x58;
    constexpr std::size_t kSlabSize = 16;
    constexpr std::size_t kPageSize = 12;
    constexpr std::size_t kPointerSize = 8;
    constexpr std::size_t kAssetSize = 72;
    constexpr std::uint32_t kTextureType =
        static_cast<std::uint32_t>('t') | (static_cast<std::uint32_t>('x') << 8) |
        (static_cast<std::uint32_t>('t') << 16) | (static_cast<std::uint32_t>('r') << 24);
    if (rpak.size() < kHeaderSize || std::memcmp(rpak.data(), "RPak", 4) != 0 ||
        RpakReadU16(rpak.data() + 4) != 7) {
        error = "not a Titanfall 2 v7 RPak";
        return false;
    }
    const std::uint16_t headerFlags = RpakReadU16(rpak.data() + 6);
    if ((headerFlags & ~kRpakModuleFlag) != 0 || RpakReadU16(rpak.data() + 0x3e) != 0) {
        error = "compressed and patch RPaks are not supported";
        return false;
    }
    // No module is loaded for a mod pak (see kRpakModuleFlag).
    RpakWriteU16(rpak.data() + 6, 0);
    const std::size_t pathSize = RpakReadU16(rpak.data() + 0x38);
    const std::size_t slabCount = RpakReadU16(rpak.data() + 0x3a);
    const std::size_t pageCount = RpakReadU16(rpak.data() + 0x3c);
    const std::size_t pointerCount = RpakReadU32(rpak.data() + 0x40);
    const std::size_t assetCount = RpakReadU32(rpak.data() + 0x44);
    const std::size_t usesCount = RpakReadU32(rpak.data() + 0x48);
    const std::size_t dependentsCount = RpakReadU32(rpak.data() + 0x4c);
    std::size_t cursor = kHeaderSize;
    if (!RpakAdvance(cursor, pathSize, 1, rpak.size())) { error = "bad path table"; return false; }
    const std::size_t slabsOffset = cursor;
    if (!RpakAdvance(cursor, slabCount, kSlabSize, rpak.size())) { error = "bad slab table"; return false; }
    const std::size_t pagesOffset = cursor;
    if (!RpakAdvance(cursor, pageCount, kPageSize, rpak.size())) { error = "bad page table"; return false; }
    const std::size_t pointersOffset = cursor;
    if (!RpakAdvance(cursor, pointerCount, kPointerSize, rpak.size())) { error = "bad pointer table"; return false; }
    const std::size_t assetsOffset = cursor;
    if (!RpakAdvance(cursor, assetCount, kAssetSize, rpak.size())) { error = "bad asset table"; return false; }
    const std::size_t usesOffset = cursor;
    if (!RpakAdvance(cursor, usesCount, kPointerSize, rpak.size()) ||
        !RpakAdvance(cursor, dependentsCount, 4, rpak.size())) { error = "bad relation tables"; return false; }
    const std::size_t pageDataOffset = cursor;

    std::vector<std::size_t> pageOffsets(pageCount), pageSizes(pageCount);
    std::size_t pageCursor = pageDataOffset;
    for (std::size_t page = 0; page < pageCount; ++page) {
        pageOffsets[page] = pageCursor;
        pageSizes[page] = RpakReadU32(rpak.data() + pagesOffset + page * kPageSize + 8);
        if (!RpakAdvance(pageCursor, pageSizes[page], 1, rpak.size())) {
            error = "page data is truncated";
            return false;
        }
    }
    if (pageCursor != rpak.size()) { error = "unexpected data after the final page"; return false; }

    std::vector<RpakTextureConversionSpec> textures;
    for (std::size_t i = 0; i < assetCount; ++i) {
        const std::size_t descriptor = assetsOffset + i * kAssetSize;
        if (RpakReadU32(rpak.data() + descriptor + 68) != kTextureType) continue;
        RpakTextureConversionSpec texture;
        texture.descriptor = descriptor;
        texture.headPage = RpakReadU32(rpak.data() + descriptor + 16);
        texture.headOffset = RpakReadU32(rpak.data() + descriptor + 20);
        texture.cpuPage = RpakReadU32(rpak.data() + descriptor + 24);
        texture.cpuOffset = RpakReadU32(rpak.data() + descriptor + 28);
        texture.packedStream = RpakReadU64(rpak.data() + descriptor + 32);
        const std::size_t headSize = RpakReadU32(rpak.data() + descriptor + 60);
        if (texture.headPage >= pageCount || texture.cpuPage >= pageCount || headSize < 56 ||
            texture.headOffset > pageSizes[texture.headPage] ||
            headSize > pageSizes[texture.headPage] - texture.headOffset) {
            error = "texture descriptor points outside a page";
            return false;
        }
        const std::size_t header = pageOffsets[texture.headPage] + texture.headOffset;
        const std::uint8_t platform = rpak[header + 28];
        if (platform == 8) continue;
        if (platform != 0) { error = "unsupported texture platform " + std::to_string(platform); return false; }
        texture.width = RpakReadU16(rpak.data() + header + 16);
        texture.height = RpakReadU16(rpak.data() + header + 18);
        texture.format = RpakReadU16(rpak.data() + header + 22);
        texture.arrays = std::max<std::uint8_t>(1, rpak[header + 30]);
        texture.permanentMips = rpak[header + 33];
        texture.streamedMips = rpak[header + 34];
        if (!texture.width || !texture.height || !texture.permanentMips ||
            texture.permanentMips + texture.streamedMips > 13 || texture.arrays != 1) {
            error = "unsupported texture dimensions, mip count, or array size";
            return false;
        }
        std::size_t bytesPerBlock = 0, blockPixels = 0;
        if (!RpakTextureFormat(texture.format, bytesPerBlock, blockPixels)) {
            error = "unsupported texture format " + std::to_string(texture.format);
            return false;
        }
        const std::size_t totalMips = texture.permanentMips + texture.streamedMips;
        for (std::size_t mip = 0; mip < totalMips; ++mip) {
            const std::size_t width = std::max<std::size_t>(1, texture.width >> mip);
            const std::size_t height = std::max<std::size_t>(1, texture.height >> mip);
            const std::size_t tiled = RpakPs4MipSize(width, height, bytesPerBlock, blockPixels);
            if (!tiled || tiled > std::numeric_limits<std::size_t>::max() - texture.newTotalSize) {
                error = "texture size overflow";
                return false;
            }
            texture.newTotalSize += tiled;
        }
        if (texture.cpuOffset > pageSizes[texture.cpuPage]) { error = "texture CPU pointer is outside its page"; return false; }
        const std::size_t cpu = pageOffsets[texture.cpuPage] + texture.cpuOffset;
        if (!RpakConvertMipGroup(rpak.data() + cpu, pageSizes[texture.cpuPage] - texture.cpuOffset,
                texture, texture.streamedMips, texture.permanentMips,
                texture.permanent, texture.oldPermanentSize, error)) return false;
        textures.push_back(std::move(texture));
    }

    if (textures.empty()) return true;
    std::vector<std::vector<RpakPageReplacement>> replacements(pageCount);
    struct StreamRequest {
        std::size_t index = 0;
        std::size_t offset = 0;
        std::vector<std::uint8_t> data;
    };
    std::map<std::pair<std::size_t, std::size_t>, StreamRequest> streamRequests;
    for (auto& texture : textures) {
        RpakPageReplacement replacement;
        replacement.start = texture.cpuOffset;
        replacement.oldSize = texture.oldPermanentSize;
        replacement.data = texture.permanent;
        replacements[texture.cpuPage].push_back(std::move(replacement));
        report.permanentBytesBefore += texture.oldPermanentSize;
        report.permanentBytesAfter += texture.permanent.size();

        if (texture.streamedMips) {
            if (texture.packedStream == ~std::uint64_t(0)) { error = "streamed texture has no STARPak offset"; return false; }
            const std::size_t streamIndex = static_cast<std::size_t>(texture.packedStream & 0xfff);
            const std::size_t streamOffset = static_cast<std::size_t>(texture.packedStream & ~std::uint64_t(0xfff));
            if (streamIndex >= starpaks.size() || streamOffset >= starpaks[streamIndex].size()) {
                error = "texture refers to a missing STARPak block";
                return false;
            }
            std::vector<std::uint8_t> converted;
            std::size_t consumed = 0;
            if (!RpakConvertMipGroup(starpaks[streamIndex].data() + streamOffset,
                    starpaks[streamIndex].size() - streamOffset, texture, 0,
                    texture.streamedMips, converted, consumed, error)) return false;
            const auto key = std::make_pair(streamIndex, streamOffset);
            const auto found = streamRequests.find(key);
            if (found != streamRequests.end()) {
                if (found->second.data != converted) { error = "shared STARPak block has conflicting texture layouts"; return false; }
            } else {
                streamRequests.emplace(key, StreamRequest{streamIndex, streamOffset, std::move(converted)});
            }
        }
    }

    // Validate STARPak tables and then swizzle only referenced blocks. Keeping
    // their 4 KiB allocations and offsets intact also preserves non-texture
    // assets and cross-RPak references.
    for (auto& requestEntry : streamRequests) {
        StreamRequest& request = requestEntry.second;
        auto& starpak = starpaks[request.index];
        if (starpak.size() < 16 || std::memcmp(starpak.data(), "SRPk", 4) != 0 ||
            RpakReadU32(starpak.data() + 4) != 1) { error = "invalid STARPak"; return false; }
        const std::uint64_t entryCount64 = RpakReadU64(starpak.data() + starpak.size() - 8);
        if (entryCount64 > (starpak.size() - 8) / 16) { error = "invalid STARPak entry count"; return false; }
        const std::size_t entryCount = static_cast<std::size_t>(entryCount64);
        const std::size_t table = starpak.size() - 8 - entryCount * 16;
        std::size_t allocation = 0;
        for (std::size_t i = 0; i < entryCount; ++i) {
            const std::size_t entry = table + i * 16;
            if (RpakReadU64(starpak.data() + entry) == request.offset) {
                allocation = static_cast<std::size_t>(RpakReadU64(starpak.data() + entry + 8));
                break;
            }
        }
        if (!allocation || request.offset > table || allocation > table - request.offset ||
            request.data.size() > allocation) { error = "converted texture does not fit its STARPak allocation"; return false; }
        std::fill(starpak.begin() + request.offset, starpak.begin() + request.offset + allocation, 0);
        std::copy(request.data.begin(), request.data.end(), starpak.begin() + request.offset);
        ++report.streamedBlocks;
    }

    for (std::size_t page = 0; page < pageCount; ++page) {
        auto& pageReplacements = replacements[page];
        std::sort(pageReplacements.begin(), pageReplacements.end(),
            [](const auto& a, const auto& b) { return a.start < b.start; });
        std::size_t end = 0;
        for (const auto& replacement : pageReplacements) {
            if (replacement.start < end || replacement.start > pageSizes[page] ||
                replacement.oldSize > pageSizes[page] - replacement.start) {
                error = "overlapping or out-of-range texture page data";
                return false;
            }
            end = replacement.start + replacement.oldSize;
        }
    }

    auto mapOffset = [&](std::size_t page, std::size_t offset, std::size_t& mapped) -> bool {
        if (page >= pageCount || offset > pageSizes[page]) return false;
        std::int64_t delta = 0;
        for (const auto& replacement : replacements[page]) {
            const std::size_t end = replacement.start + replacement.oldSize;
            if (offset > replacement.start && offset < end) return false;
            if (end <= offset) delta += static_cast<std::int64_t>(replacement.data.size()) -
                static_cast<std::int64_t>(replacement.oldSize);
        }
        const std::int64_t result = static_cast<std::int64_t>(offset) + delta;
        if (result < 0) return false;
        mapped = static_cast<std::size_t>(result);
        return true;
    };

    std::vector<std::vector<std::uint8_t>> newPages(pageCount);
    std::vector<std::int64_t> pageDeltas(pageCount, 0);
    for (std::size_t page = 0; page < pageCount; ++page) {
        std::size_t source = 0;
        for (const auto& replacement : replacements[page]) {
            newPages[page].insert(newPages[page].end(),
                rpak.begin() + pageOffsets[page] + source,
                rpak.begin() + pageOffsets[page] + replacement.start);
            newPages[page].insert(newPages[page].end(), replacement.data.begin(), replacement.data.end());
            source = replacement.start + replacement.oldSize;
        }
        newPages[page].insert(newPages[page].end(), rpak.begin() + pageOffsets[page] + source,
            rpak.begin() + pageOffsets[page] + pageSizes[page]);
        pageDeltas[page] = static_cast<std::int64_t>(newPages[page].size()) -
            static_cast<std::int64_t>(pageSizes[page]);
    }

    // Pointer descriptors identify PagePtr fields inside page data. Relocate
    // both the descriptor and the PagePtr value it identifies.
    for (std::size_t i = 0; i < pointerCount; ++i) {
        const std::size_t pointer = pointersOffset + i * 8;
        const std::size_t page = RpakReadU32(rpak.data() + pointer);
        const std::size_t oldOffset = RpakReadU32(rpak.data() + pointer + 4);
        std::size_t newOffset = 0;
        if (!mapOffset(page, oldOffset, newOffset) || newOffset + 8 > newPages[page].size()) {
            error = "pointer descriptor intersects converted texture data";
            return false;
        }
        RpakWriteU32(rpak.data() + pointer + 4, static_cast<std::uint32_t>(newOffset));
        std::uint8_t* value = newPages[page].data() + newOffset;
        const std::uint32_t targetPage = RpakReadU32(value);
        const std::uint32_t targetOffset = RpakReadU32(value + 4);
        if (targetPage != std::numeric_limits<std::uint32_t>::max()) {
            std::size_t mappedTarget = 0;
            if (!mapOffset(targetPage, targetOffset, mappedTarget)) { error = "PagePtr target intersects converted texture data"; return false; }
            RpakWriteU32(value + 4, static_cast<std::uint32_t>(mappedTarget));
        }
    }

    for (std::size_t i = 0; i < usesCount; ++i) {
        const std::size_t use = usesOffset + i * 8;
        const std::size_t page = RpakReadU32(rpak.data() + use);
        const std::size_t oldOffset = RpakReadU32(rpak.data() + use + 4);
        std::size_t mapped = 0;
        if (!mapOffset(page, oldOffset, mapped)) { error = "asset-use pointer intersects converted texture data"; return false; }
        RpakWriteU32(rpak.data() + use + 4, static_cast<std::uint32_t>(mapped));
    }

    for (std::size_t i = 0; i < assetCount; ++i) {
        const std::size_t descriptor = assetsOffset + i * kAssetSize;
        for (std::size_t field : {std::size_t(16), std::size_t(24)}) {
            const std::size_t page = RpakReadU32(rpak.data() + descriptor + field);
            if (page == std::numeric_limits<std::uint32_t>::max()) continue;
            const std::size_t oldOffset = RpakReadU32(rpak.data() + descriptor + field + 4);
            std::size_t mapped = 0;
            if (!mapOffset(page, oldOffset, mapped)) { error = "asset pointer intersects converted texture data"; return false; }
            RpakWriteU32(rpak.data() + descriptor + field + 4, static_cast<std::uint32_t>(mapped));
        }
    }

    for (auto& texture : textures) {
        std::size_t headerOffset = 0;
        if (!mapOffset(texture.headPage, texture.headOffset, headerOffset) || headerOffset + 56 > newPages[texture.headPage].size()) {
            error = "converted texture header is invalid";
            return false;
        }
        std::uint8_t* header = newPages[texture.headPage].data() + headerOffset;
        RpakWriteU32(header + 24, static_cast<std::uint32_t>(texture.newTotalSize));
        header[28] = 8;
        header[32] |= 1;
        ++report.textures;
    }

    for (std::size_t page = 0; page < pageCount; ++page) {
        if (newPages[page].size() > std::numeric_limits<std::uint32_t>::max()) { error = "converted page is too large"; return false; }
        RpakWriteU32(rpak.data() + pagesOffset + page * kPageSize + 8,
            static_cast<std::uint32_t>(newPages[page].size()));
    }
    for (std::size_t slab = 0; slab < slabCount; ++slab) {
        std::int64_t delta = 0;
        for (std::size_t page = 0; page < pageCount; ++page) {
            if (RpakReadU32(rpak.data() + pagesOffset + page * kPageSize) == slab) delta += pageDeltas[page];
        }
        const std::uint64_t oldSize = RpakReadU64(rpak.data() + slabsOffset + slab * kSlabSize + 8);
        if (delta < 0 && static_cast<std::uint64_t>(-delta) > oldSize) { error = "converted slab size underflow"; return false; }
        RpakWriteU64(rpak.data() + slabsOffset + slab * kSlabSize + 8,
            static_cast<std::uint64_t>(static_cast<std::int64_t>(oldSize) + delta));
    }

    rpak.resize(pageDataOffset);
    for (const auto& page : newPages) rpak.insert(rpak.end(), page.begin(), page.end());
    RpakWriteU64(rpak.data() + 0x18, rpak.size());
    RpakWriteU64(rpak.data() + 0x28, rpak.size());
    return true;
}

}  // namespace northstar::ps4::mods
