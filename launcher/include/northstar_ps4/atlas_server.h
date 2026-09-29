#pragma once

// The parts of a hosted server's Atlas protocol that do not touch the game or
// the network, so they can be host-tested (tests/atlas_server.cpp). Sources:
// PC Northstar's server/servernethooks.cpp (HMAC-SHA256-signed "sigreq1"
// connectionless packets), server/serverpresence.cpp (UnescapeUnicode) and
// masterserver/masterserver.cpp (add_server / update_values).

#include "northstar_ps4/http_request.h"
#include "northstar_ps4/mod_download.h"
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace northstar::ps4::atlas {

constexpr std::size_t kHmacSha256Length = 32;

// HMAC-SHA256 (RFC 2104) over raw bytes.
inline void HmacSha256(const std::string& key, const std::string& data, std::uint8_t out[kHmacSha256Length]) {
    std::uint8_t block[64] = {};
    if (key.size() > sizeof(block)) {
        mods::Sha256 hash;
        hash.Update(key.data(), key.size());
        hash.Finish(block);
    } else {
        std::memcpy(block, key.data(), key.size());
    }
    std::uint8_t inner[64], outer[64];
    for (int i = 0; i < 64; ++i) {
        inner[i] = static_cast<std::uint8_t>(block[i] ^ 0x36);
        outer[i] = static_cast<std::uint8_t>(block[i] ^ 0x5c);
    }
    std::uint8_t innerDigest[kHmacSha256Length];
    mods::Sha256 first;
    first.Update(inner, sizeof(inner));
    first.Update(data.data(), data.size());
    first.Finish(innerDigest);
    mods::Sha256 second;
    second.Update(outer, sizeof(outer));
    second.Update(innerDigest, sizeof(innerDigest));
    second.Finish(out);
}

// PC's VerifyHMACSHA256: constant-time comparison of a raw signature.
inline bool VerifyHmacSha256(const std::string& key, const std::string& signature, const std::string& data) {
    std::uint8_t digest[kHmacSha256Length];
    HmacSha256(key, data, digest);
    if (signature.size() != kHmacSha256Length) return false;
    std::uint8_t difference = 0;
    for (std::size_t i = 0; i < kHmacSha256Length; ++i)
        difference |= static_cast<std::uint8_t>(signature[i]) ^ digest[i];
    return difference == 0;
}

// A 'T' connectionless packet: 0xFFFFFFFF, 'T', a NUL-terminated type, data.
// PC's ProcessAtlasConnectionlessPacket; false when there is no NUL after the
// type.
inline bool ParseAtlasPacket(const std::uint8_t* packet, std::size_t size, std::string& type, std::string& data) {
    type.clear();
    data.clear();
    if (size <= 4 || packet[4] != 'T') return false;
    for (std::size_t i = 5; i < size; ++i) {
        if (packet[i] == 0) {
            type.assign(reinterpret_cast<const char*>(packet + 5), i - 5);
            if (i + 1 < size) data.assign(reinterpret_cast<const char*>(packet + i + 1), size - i - 1);
            return true;
        }
    }
    return false;
}

// A client's connect request: 0xFFFFFFFF, 'A', four 32-bit fields, the 64-bit
// uid (little-endian), the player name, then NUL-terminated strings, one of
// which is the serverfilter convar (the Atlas connection token). The PS4 client
// (engine+0x1554c3) writes serverfilter right after the name; a PC client sends
// another, empty, string first. So the strings after the name are all kept and
// the token is matched against each.
struct ConnectRequest {
    std::uint64_t uid = 0;
    std::string name;
    std::vector<std::string> strings;  // after the name, up to kMaxConnectStrings

    bool HasString(const std::string& text) const {
        for (const auto& value : strings)
            if (value == text) return true;
        return false;
    }
};
constexpr std::size_t kMaxConnectStrings = 4;

inline bool ParseConnectRequest(const std::uint8_t* packet, std::size_t size, ConnectRequest& out) {
    constexpr std::size_t kUidOffset = 21;
    constexpr std::size_t kNameOffset = kUidOffset + 8;
    if (size <= kNameOffset || packet[0] != 0xff || packet[1] != 0xff || packet[2] != 0xff || packet[3] != 0xff ||
        packet[4] != 'A')
        return false;
    out.uid = 0;
    for (int i = 7; i >= 0; --i) out.uid = (out.uid << 8) | packet[kUidOffset + i];
    std::size_t at = kNameOffset;
    auto readString = [&](std::string& text) {
        const std::size_t start = at;
        while (at < size && packet[at] != 0) ++at;
        if (at >= size) return false;
        text.assign(reinterpret_cast<const char*>(packet + start), at - start);
        ++at;
        return true;
    };
    if (!readString(out.name)) return false;
    out.strings.clear();
    std::string value;
    while (out.strings.size() < kMaxConnectStrings && readString(value)) out.strings.push_back(value);
    return true;
}

// PC's UnescapeUnicode: \uXXXX sequences in ns_server_name / ns_server_desc
// become UTF-8 (BMP only, as PC).
inline std::string UnescapeUnicode(const std::string& text) {
    auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    std::string out;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\\' && i + 5 < text.size() &&(text[i + 1] == 'u' || text[i + 1] == 'U') &&
            hex(text[i + 2]) >= 0 && hex(text[i + 3]) >= 0 && hex(text[i + 4]) >= 0 && hex(text[i + 5]) >= 0) {
            const unsigned cp = (hex(text[i + 2]) << 12) | (hex(text[i + 3]) << 8) | (hex(text[i + 4]) << 4) | hex(text[i + 5]);
            if (cp <= 0x7f) {
                out += static_cast<char>(cp);
            } else if (cp <= 0x7ff) {
                out += static_cast<char>(0xc0 | (cp >> 6));
                out += static_cast<char>(0x80 | (cp & 0x3f));
            } else {
                out += static_cast<char>(0xe0 | (cp >> 12));
                out += static_cast<char>(0x80 | ((cp >> 6) & 0x3f));
                out += static_cast<char>(0x80 | (cp & 0x3f));
            }
            i += 5;
        } else {
            out += text[i];
        }
    }
    return out;
}

struct Presence {
    int port = 0;
    std::string name;
    std::string description;
    std::string map;
    std::string playlist;
    std::string password;
    int playerCount = 0;
    int maxPlayers = 0;
};

// The query of add_server (id empty) or update_values, in PC's order.
inline std::string PresenceQuery(const Presence& presence, const std::string& id) {
    std::string query;
    if (!id.empty()) query += "id=" + http::UrlEscape(id) + "&";
    query += "port=" + std::to_string(presence.port) + "&authPort=udp&name=" + http::UrlEscape(presence.name) +
        "&description=" + http::UrlEscape(presence.description) + "&map=" + http::UrlEscape(presence.map) +
        "&playlist=" + http::UrlEscape(presence.playlist);
    if (!id.empty()) query += "&playerCount=" + std::to_string(presence.playerCount);
    query += "&maxPlayers=" + std::to_string(presence.maxPlayers) + "&password=" + http::UrlEscape(presence.password);
    return query;
}

// curl_mime with one part: name "modinfo", filename "modinfo.json",
// application/json.
inline std::string ModInfoMultipart(const std::string& boundary, const std::string& modInfo) {
    return "--" + boundary +
        "\r\nContent-Disposition: form-data; name=\"modinfo\"; filename=\"modinfo.json\"\r\n"
        "Content-Type: application/json\r\n\r\n" +
        modInfo + "\r\n--" + boundary + "--\r\n";
}

} // namespace northstar::ps4::atlas
