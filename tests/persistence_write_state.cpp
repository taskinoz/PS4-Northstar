#include "northstar_ps4/persistence_write_state.h"

#include <cassert>
#include <cstdio>
#include <string>

using namespace northstar::ps4::persistence;

int main() {
    std::string acknowledged = "original";
    std::string queued;
    assert(ClassifyWrite(acknowledged, queued, acknowledged) == WriteDecision::Unchanged);
    assert(ClassifyWrite(acknowledged, queued, "change-a") == WriteDecision::Queue);

    queued = "change-a";
    assert(ClassifyWrite(acknowledged, queued, "change-a") == WriteDecision::AlreadyQueued);
    assert(ClassifyWrite(acknowledged, queued, "change-b") == WriteDecision::Queue);

    // A failed latest request stays dirty and becomes eligible for retry.
    assert(CompleteWrite(acknowledged, queued, "change-a", false));
    assert(acknowledged == "original" && queued.empty());
    assert(ClassifyWrite(acknowledged, queued, "change-a") == WriteDecision::Queue);

    // A successful request advances the acknowledged baseline.
    queued = "change-a";
    assert(CompleteWrite(acknowledged, queued, "change-a", true));
    assert(acknowledged == "change-a" && queued.empty());
    assert(ClassifyWrite(acknowledged, queued, "change-a") == WriteDecision::Unchanged);

    // Out-of-order completion cannot overwrite or clear a newer snapshot.
    queued = "change-c";
    assert(!CompleteWrite(acknowledged, queued, "change-b", true));
    assert(acknowledged == "change-a" && queued == "change-c");
    assert(CompleteWrite(acknowledged, queued, "change-c", true));
    assert(acknowledged == "change-c" && queued.empty());

    std::puts("persistence_write_state tests passed");
    return 0;
}
