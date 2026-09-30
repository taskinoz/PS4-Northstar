#pragma once

// Pure slot-selection policy for Atlas persistence. Kept separate from the
// engine hook so duplicate-account ownership can be host-tested.

#include <cstddef>
#include <cstdint>

namespace northstar::ps4::persistence {

struct SlotCandidate {
    std::uint64_t uid = 0;
    std::int32_t signon = 0;
    bool fake = false;
    bool installed = false;
};

// A self-auth record belongs only to the listen host (slot 0). Records created
// by Atlas /server/connect belong only to remote slots. Prefer a matching slot
// that does not already own a save, so two remote clients using the same Atlas
// account are assigned independently; the second pass handles connect retries.
inline int SelectSlot(const SlotCandidate* slots, std::size_t count, std::uint64_t uid, bool hostConnection) {
    if (!slots || count == 0) return -1;
    const std::size_t begin = hostConnection ? 0 : 1;
    const std::size_t end = hostConnection ? 1 : count;
    if (begin >= count) return -1;
    for (int pass = 0; pass < 2; ++pass) {
        for (std::size_t client = begin; client < end; ++client) {
            const auto& slot = slots[client];
            if (slot.uid != uid || slot.fake || slot.signon < 1) continue;
            if (pass == 0 && slot.installed) continue;
            return static_cast<int>(client);
        }
    }
    return -1;
}

inline bool ScopeMatchesClient(bool hostConnection, int client) {
    return hostConnection ? client == 0 : client > 0;
}

} // namespace northstar::ps4::persistence
