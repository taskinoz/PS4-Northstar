#include "northstar_ps4/sq_value.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>

using namespace northstar::ps4;

int main() {
    alignas(8) char storage[0x50]{};
    std::strcpy(storage + 0x30, "ui/menu/example");

    const auto pointer = reinterpret_cast<std::uint64_t>(storage);
    const SqObjectValue stringValue{kSqStringTag, pointer};
    const SqObjectValue assetValue{kSqAssetTag, pointer};
    const SqObjectValue nullAsset{kSqAssetTag, 0};

    assert(std::strcmp(SqStringValue(stringValue), "ui/menu/example") == 0);
    assert(SqAssetValue(stringValue) == nullptr);
    assert(std::strcmp(SqAssetValue(assetValue), "ui/menu/example") == 0);
    assert(SqStringValue(assetValue) == nullptr);
    assert(SqAssetValue(nullAsset) == nullptr);

    std::puts("sq_value tests passed");
    return 0;
}
