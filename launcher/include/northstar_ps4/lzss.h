#pragma once

// PC shared/exploit_fixes/exploitfixes_lzss.cpp: CLZSS::SafeUncompress
// rewritten so a malformed payload cannot copy from before the start of the
// output. The input layout is the engine's: an 8-byte header ("LZSS", the
// uncompressed size), then command bytes whose bits, low first, select a
// literal byte (0) or a back-reference (1: 12-bit distance - 1, 4-bit count -
// 1; a count of 1 ends the stream).

#include <cstdint>
#include <cstring>

namespace northstar::ps4::lzss {

constexpr std::uint32_t kId = 0x53535a4c;  // "LZSS"
constexpr int kLookShift = 4;

// Returns the number of bytes written (the header's size), or 0 if the input
// is malformed or does not fit in bufferSize. Like the engine's, the input's
// length is not passed in; the stream ends at its end marker.
inline unsigned int SafeUncompress(const unsigned char* input, unsigned char* output, unsigned int bufferSize) noexcept {
    if (!input) return 0;
    std::uint32_t id = 0, actualSize = 0;
    std::memcpy(&id, input, 4);
    std::memcpy(&actualSize, input + 4, 4);
    if (!actualSize || id != kId || actualSize > bufferSize) return 0;
    input += 8;
    unsigned int totalBytes = 0;
    int getCmdByte = 0;
    int cmdByte = 0;
    for (;;) {
        if (!getCmdByte) cmdByte = *input++;
        getCmdByte = (getCmdByte + 1) & 0x07;
        if (cmdByte & 0x01) {
            unsigned int position = static_cast<unsigned int>(*input++) << kLookShift;
            position |= (*input >> kLookShift);
            position += 1;
            const unsigned int count = (*input++ & 0x0F) + 1;
            if (count == 1) break;
            // The reference must lie within what has been written.
            if (position > totalBytes) return 0;
            totalBytes += count;
            if (totalBytes > bufferSize) return 0;
            const unsigned char* source = output - position;
            for (unsigned int i = 0; i < count; ++i) *output++ = *source++;
        } else {
            if (++totalBytes > bufferSize) return 0;
            *output++ = *input++;
        }
        cmdByte >>= 1;
    }
    return totalBytes == actualSize ? totalBytes : 0;
}

} // namespace northstar::ps4::lzss
