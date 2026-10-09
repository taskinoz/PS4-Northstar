#include "northstar_ps4/atlas_server.h"
#include <cstdio>
#include <cstdlib>
#include <string>
using namespace northstar::ps4::atlas;

static void Check(bool ok, const char* what, int line) {
    if (!ok) {
        std::fprintf(stderr, "line %d: %s\n", line, what);
        std::exit(1);
    }
}
#define CHECK(x) Check((x), #x, __LINE__)

static std::string Hex(const std::uint8_t* bytes, std::size_t size) {
    static const char kHex[] = "0123456789abcdef";
    std::string out;
    for (std::size_t i = 0; i < size; ++i) {
        out += kHex[bytes[i] >> 4];
        out += kHex[bytes[i] & 15];
    }
    return out;
}

static std::string Bytes(const char* hex) {
    std::string out;
    for (std::size_t i = 0; hex[i] && hex[i + 1]; i += 2) out += static_cast<char>(std::strtol(std::string(hex + i, 2).c_str(), nullptr, 16));
    return out;
}

int main() {
    std::uint8_t digest[kHmacSha256Length];
    // PC's own self-test in servernethooks.cpp.
    HmacSha256("test", "test", digest);
    CHECK(Hex(digest, 32) == "88cd2108b5347d973cf39cdf9053d7dd42704876d8c9a9bd8e2d168259d3ddf7");
    // RFC 4231 test case 2.
    HmacSha256("Jefe", "what do ya want for nothing?", digest);
    CHECK(Hex(digest, 32) == "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");
    // RFC 4231 test case 6: a key longer than the block.
    HmacSha256(std::string(131, '\xaa'), "Test Using Larger Than Block-Size Key - Hash Key First", digest);
    CHECK(Hex(digest, 32) == "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54");

    const std::string signature = Bytes("88cd2108b5347d973cf39cdf9053d7dd42704876d8c9a9bd8e2d168259d3ddf7");
    CHECK(VerifyHmacSha256("test", signature, "test"));
    CHECK(!VerifyHmacSha256("test", signature, "tesT"));
    CHECK(!VerifyHmacSha256("other", signature, "test"));
    CHECK(!VerifyHmacSha256("test", signature.substr(1), "test"));

    // 'T' packets.
    std::string type, data;
    const std::string packet = std::string("\xff\xff\xff\xffTsigreq1\0payload", 20);
    CHECK(ParseAtlasPacket(reinterpret_cast<const std::uint8_t*>(packet.data()), packet.size(), type, data));
    CHECK(type == "sigreq1" && data == "payload");
    const std::string empty = std::string("\xff\xff\xff\xffTx\0", 7);
    CHECK(ParseAtlasPacket(reinterpret_cast<const std::uint8_t*>(empty.data()), empty.size(), type, data) && type == "x" && data.empty());
    const std::string noNul = "\xff\xff\xff\xffTsigreq1";
    CHECK(!ParseAtlasPacket(reinterpret_cast<const std::uint8_t*>(noNul.data()), noNul.size(), type, data));
    const std::string other = std::string("\xff\xff\xff\xff" "A", 5);
    CHECK(!ParseAtlasPacket(reinterpret_cast<const std::uint8_t*>(other.data()), other.size(), type, data));

    // Connect requests: the start of one captured from the PS4 client, then
    // uid 1000000000001, the name and the token.
    std::string connect("\xff\xff\xff\xff" "A" "\x1a\x00\x00\x00" "\xd1\x07\x00\x00" "\x01\x02\x03\x04" "\x05\x06\x07\x08", 21);
    const std::uint64_t uid = 1000000000001ull;
    for (int i = 0; i < 8; ++i) connect += static_cast<char>((uid >> (8 * i)) & 0xff);
    connect += std::string("Titanfall-PS4\0" "0123456789abcdef\0", 31);
    connect += "trailing";
    ConnectRequest request;
    CHECK(ParseConnectRequest(reinterpret_cast<const std::uint8_t*>(connect.data()), connect.size(), request));
    CHECK(request.uid == uid && request.name == "Titanfall-PS4" && request.HasString("0123456789abcdef"));
    // A PC client: an empty string between the name and the token.
    std::string pc = connect.substr(0, 29) + std::string("PCPlayer\0\0" "0fedcba9876543210fedcba98765432\0", 42);
    CHECK(ParseConnectRequest(reinterpret_cast<const std::uint8_t*>(pc.data()), pc.size(), request));
    CHECK(request.name == "PCPlayer" && request.strings.size() == 2 && request.strings[0].empty() &&
          request.HasString("0fedcba9876543210fedcba98765432") && !request.HasString("other"));
    // Cut inside the name: no name, no request.
    CHECK(!ParseConnectRequest(reinterpret_cast<const std::uint8_t*>(connect.data()), 35, request));
    std::string notConnect = connect;
    notConnect[4] = 'H';
    CHECK(!ParseConnectRequest(reinterpret_cast<const std::uint8_t*>(notConnect.data()), notConnect.size(), request));

    CHECK(UnescapeUnicode("plain") == "plain");
    CHECK(UnescapeUnicode("caf\\u00e9") == "caf\xc3\xa9");
    CHECK(UnescapeUnicode("\\u4E2D!") == "\xe4\xb8\xad!");
    CHECK(UnescapeUnicode("\\u12") == "\\u12");
    CHECK(UnescapeUnicode("\\u0041\\u0042") == "AB");

    Presence presence;
    presence.port = 37015;
    presence.name = "My PS4 server";
    presence.description = "desc & more";
    presence.map = "mp_lobby";
    presence.playlist = "private_match";
    presence.password = "";
    presence.playerCount = 2;
    presence.maxPlayers = 16;
    CHECK(PresenceQuery(presence, "") ==
          "port=37015&authPort=udp&name=My%20PS4%20server&description=desc%20%26%20more&map=mp_lobby&playlist=private_match&maxPlayers=16&password=");
    CHECK(PresenceQuery(presence, "abc") ==
          "id=abc&port=37015&authPort=udp&name=My%20PS4%20server&description=desc%20%26%20more&map=mp_lobby&playlist=private_match&playerCount=2&maxPlayers=16&password=");

    const std::string body = ModInfoMultipart("B", "{\"Mods\":[]}");
    CHECK(body == "--B\r\nContent-Disposition: form-data; name=\"modinfo\"; filename=\"modinfo.json\"\r\n"
                  "Content-Type: application/json\r\n\r\n{\"Mods\":[]}\r\n--B--\r\n");

    std::puts("atlas_server tests passed");
    return 0;
}
