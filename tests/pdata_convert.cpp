#include "northstar_ps4/pdata_convert.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <vector>
using namespace northstar::ps4::pdata;

static void Check(bool ok, const char* what, int line) {
    if (!ok) {
        std::fprintf(stderr, "line %d: %s\n", line, what);
        std::exit(1);
    }
}
#define CHECK(x) Check((x), #x, __LINE__)

int main(int argc, char** argv) {
    // Atlas's own placeholder pdata when available (tools/ is not in Git),
    // otherwise a synthetic one of the same shape.
    std::string pc;
    if (argc > 1) {
        std::ifstream file(argv[1], std::ios::binary);
        pc.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    }
    if (pc.empty()) {
        pc.assign(56306, '\0');
        pc[0] = static_cast<char>(231);
        for (std::size_t i = 4; i < kPc231Size; ++i) pc[i] = static_cast<char>(i * 7);
    }
    CHECK(pc.size() == 56306);
    // Mark the trailing bytes so they can be told apart from the black market.
    for (std::size_t i = kPc231Size; i < pc.size(); ++i) pc[i] = static_cast<char>(0x40 + (i & 15));
    CHECK(CanInstall(pc));

    std::vector<std::uint8_t> buffer(kBufferSize, 0xcc);
    const std::string trailing = InstallFromPc(pc, buffer.data());
    CHECK(trailing.size() == 137);
    CHECK(std::memcmp(buffer.data(), pc.data(), kPc231Size) == 0);
    // The black market (and the rest of the buffer) starts empty, not with
    // the PC trailing bytes.
    bool blackMarketEmpty = true;
    for (std::size_t i = kPc231Size; i < kBufferSize; ++i) blackMarketEmpty = blackMarketEmpty && buffer[i] == 0;
    CHECK(blackMarketEmpty);

    // A game changes 231 values and the black market; the write back carries
    // the 231 change and the original trailing bytes, not the black market.
    buffer[8] = 0x55;
    buffer[kPc231Size + 3] = 0x77;
    const std::string back = ExportToPc(buffer.data(), trailing);
    CHECK(back.size() == pc.size());
    CHECK(back[8] == 0x55);
    CHECK(back.compare(kPc231Size, std::string::npos, trailing) == 0);
    CHECK(std::memcmp(back.data() + 9, pc.data() + 9, kPc231Size - 9) == 0);

    // Refusals.
    std::string wrongVersion = pc;
    wrongVersion[0] = static_cast<char>(232);
    CHECK(!CanInstall(wrongVersion));
    CHECK(!CanInstall(pc.substr(0, kPc231Size - 1)));
    CHECK(CanInstall(pc.substr(0, kPc231Size)));
    CHECK(!CanInstall(std::string(kBufferSize + 1, '\0')));
    std::vector<std::uint8_t> reset(kBufferSize, 0);
    CHECK(ExportToPc(reset.data(), trailing).empty());

    std::puts("pdata_convert tests passed");
    return 0;
}
