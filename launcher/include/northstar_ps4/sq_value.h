#pragma once

#include <cstdint>

namespace northstar::ps4 {

// Titanfall's SQObject is a 16-byte tagged value. Strings and assets both
// point at ref-counted objects whose character data begins at +0x30, but
// their tags are deliberately distinct and the typed VM getters enforce it.
struct SqObjectValue {
    std::uint64_t tag;
    std::uint64_t value;
};

constexpr std::uint64_t kSqStringTag = 0x08000010;
constexpr std::uint64_t kSqAssetTag = 0x08000400;

inline const char* SqTextValue(const SqObjectValue& object,
    std::uint64_t expectedTag) noexcept {
    return object.tag == expectedTag && object.value
        ? reinterpret_cast<const char*>(object.value + 0x30)
        : nullptr;
}

inline const char* SqStringValue(const SqObjectValue& object) noexcept {
    return SqTextValue(object, kSqStringTag);
}

inline const char* SqAssetValue(const SqObjectValue& object) noexcept {
    return SqTextValue(object, kSqAssetTag);
}

} // namespace northstar::ps4
