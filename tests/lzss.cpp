#include "northstar_ps4/lzss.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
using namespace northstar::ps4::lzss;

static void Check(bool ok, const char* what, int line) {
    if (!ok) {
        std::fprintf(stderr, "line %d: %s\n", line, what);
        std::exit(1);
    }
}
#define CHECK(x) Check((x), #x, __LINE__)

// Builds a stream from tokens: a literal byte, or a back-reference
// (distance 1..4096, count 2..16). An end marker is appended.
struct Token { bool reference; unsigned char literal; unsigned int distance; unsigned int count; };
static std::vector<unsigned char> Encode(std::uint32_t size, const std::vector<Token>& tokens, std::uint32_t id = kId) {
    std::vector<unsigned char> out(8);
    std::memcpy(out.data(), &id, 4);
    std::memcpy(out.data() + 4, &size, 4);
    std::vector<Token> all = tokens;
    all.push_back({true, 0, 1, 1});  // end marker: count 1
    for (std::size_t i = 0; i < all.size(); i += 8) {
        unsigned char command = 0;
        const std::size_t commandAt = out.size();
        out.push_back(0);
        for (std::size_t j = 0; j < 8 && i + j < all.size(); ++j) {
            const Token& t = all[i + j];
            if (t.reference) {
                command |= static_cast<unsigned char>(1u << j);
                const unsigned int position = t.distance - 1;
                out.push_back(static_cast<unsigned char>(position >> 4));
                out.push_back(static_cast<unsigned char>(((position & 0xf) << 4) | ((t.count - 1) & 0xf)));
            } else {
                out.push_back(t.literal);
            }
        }
        out[commandAt] = command;
    }
    return out;
}

static std::vector<Token> Literals(const std::string& text) {
    std::vector<Token> tokens;
    for (char c : text) tokens.push_back({false, static_cast<unsigned char>(c), 0, 0});
    return tokens;
}

int main() {
    unsigned char out[64];

    // Literals only.
    {
        const auto stream = Encode(5, Literals("hello"));
        std::memset(out, 0, sizeof(out));
        CHECK(SafeUncompress(stream.data(), out, sizeof(out)) == 5);
        CHECK(std::memcmp(out, "hello", 5) == 0);
    }
    // A back-reference into what was written: "abc" + copy 3 from 3 back.
    {
        auto tokens = Literals("abc");
        tokens.push_back({true, 0, 3, 3});
        const auto stream = Encode(6, tokens);
        std::memset(out, 0, sizeof(out));
        CHECK(SafeUncompress(stream.data(), out, sizeof(out)) == 6);
        CHECK(std::memcmp(out, "abcabc", 6) == 0);
    }
    // An overlapping run: "a" + copy 8 from 1 back.
    {
        auto tokens = Literals("a");
        tokens.push_back({true, 0, 1, 8});
        const auto stream = Encode(9, tokens);
        CHECK(SafeUncompress(stream.data(), out, sizeof(out)) == 9);
        CHECK(std::memcmp(out, "aaaaaaaaa", 9) == 0);
    }
    // A reference to before the start of the output is refused (the PC fix).
    {
        auto tokens = Literals("ab");
        tokens.push_back({true, 0, 40, 4});
        const auto stream = Encode(6, tokens);
        CHECK(SafeUncompress(stream.data(), out, sizeof(out)) == 0);
    }
    // A reference to exactly the first byte is fine; one further is not.
    {
        auto good = Literals("xy");
        good.push_back({true, 0, 2, 2});
        CHECK(SafeUncompress(Encode(4, good).data(), out, sizeof(out)) == 4);
        auto bad = Literals("xy");
        bad.push_back({true, 0, 3, 2});
        CHECK(SafeUncompress(Encode(4, bad).data(), out, sizeof(out)) == 0);
    }
    // Header checks: wrong id, zero size, size larger than the buffer.
    {
        CHECK(SafeUncompress(Encode(5, Literals("hello"), 0x12345678).data(), out, sizeof(out)) == 0);
        CHECK(SafeUncompress(Encode(0, Literals("")).data(), out, sizeof(out)) == 0);
        CHECK(SafeUncompress(Encode(5, Literals("hello")).data(), out, 4) == 0);
        CHECK(SafeUncompress(nullptr, out, sizeof(out)) == 0);
    }
    // Output larger than the buffer, though the header claims it fits.
    {
        auto tokens = Literals("abcd");
        tokens.push_back({true, 0, 4, 16});
        CHECK(SafeUncompress(Encode(4, tokens).data(), out, 8) == 0);
    }
    // The decoded length must match the header.
    {
        CHECK(SafeUncompress(Encode(6, Literals("hello")).data(), out, sizeof(out)) == 0);
    }
    std::puts("lzss tests passed");
    return 0;
}
