#pragma once

#include <string>

namespace northstar::ps4::persistence {

enum class WriteDecision { Unchanged, AlreadyQueued, Queue };

inline WriteDecision ClassifyWrite(const std::string& acknowledged,
    const std::string& queued, const std::string& current) noexcept {
    if (current == acknowledged) return WriteDecision::Unchanged;
    if (current == queued) return WriteDecision::AlreadyQueued;
    return WriteDecision::Queue;
}

// Complete only the latest submitted snapshot. An older request finishing
// after a newer one must not move the baseline backwards or clear its marker.
inline bool CompleteWrite(std::string& acknowledged, std::string& queued,
    const std::string& completed, bool accepted) {
    if (queued != completed) return false;
    if (accepted) acknowledged = completed;
    queued.clear();
    return true;
}

} // namespace northstar::ps4::persistence
