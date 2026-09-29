#pragma once

// Moving a player's saved data (pdata) between Atlas and a PS4-hosted server.
// Host-tested in tests/pdata_convert.cpp.
//
// PC and Atlas use persistence layout 231: a 56,169-byte structure (Atlas
// pkg/pdata, UnmarshalBinary) whose first int is the layout version, 231,
// followed by trailing bytes Atlas keeps as they are (its placeholder pdata is
// 56,306 bytes in all; PC mods' pdiff fields live there).
//
// The PS4 server uses Northstar.PS4's persistent_player_data_version_929.pdef,
// which is 231 verbatim followed by the console's black market (bm, 181
// bytes): 56,350 bytes. The 231 range lines up byte for byte (checked by
// comparing every root member, enum and struct of both definitions), so:
//   - Atlas -> PS4: the 231 structure is copied as is and the black market
//     starts empty; the trailing bytes, which would land on the black market,
//     are kept aside instead;
//   - PS4 -> Atlas: the 231 structure is copied back and the kept trailing
//     bytes are appended, so the save keeps its original size and whatever a
//     PC mod stored there.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

namespace northstar::ps4::pdata {

constexpr std::int32_t kPcVersion = 231;
constexpr std::size_t kPc231Size = 56169;
constexpr std::size_t kPs4Size = 56350;            // 231 + bm
constexpr std::size_t kBufferSize = 56781;         // the engine's per-player buffer (PC PERSISTENCE_MAX_SIZE)

inline std::int32_t Version(const std::uint8_t* data) {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(data[0]) | (static_cast<std::uint32_t>(data[1]) << 8) |
        (static_cast<std::uint32_t>(data[2]) << 16) | (static_cast<std::uint32_t>(data[3]) << 24));
}

// Whether Atlas's pdata can be installed: layout 231 and at least its full
// structure, and no more than the buffer holds.
inline bool CanInstall(const std::string& pc) {
    return pc.size() >= kPc231Size && pc.size() <= kBufferSize &&
        Version(reinterpret_cast<const std::uint8_t*>(pc.data())) == kPcVersion;
}

// Writes the PS4 layout into `buffer` (kBufferSize bytes) and returns the
// trailing bytes to keep for the write back. `pc` must pass CanInstall.
inline std::string InstallFromPc(const std::string& pc, std::uint8_t* buffer) {
    std::memcpy(buffer, pc.data(), kPc231Size);
    std::memset(buffer + kPc231Size, 0, kBufferSize - kPc231Size);
    return pc.substr(kPc231Size);
}

// The pdata to write back to Atlas from the PS4 buffer, or empty when the
// buffer no longer holds a layout-231 save.
inline std::string ExportToPc(const std::uint8_t* buffer, const std::string& trailing) {
    if (Version(buffer) != kPcVersion) return {};
    std::string out(reinterpret_cast<const char*>(buffer), kPc231Size);
    out += trailing;
    return out;
}

} // namespace northstar::ps4::pdata
