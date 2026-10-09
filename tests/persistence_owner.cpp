#include "northstar_ps4/persistence_owner.h"
#include <cstdio>
#include <cstdlib>

using namespace northstar::ps4::persistence;

static void Check(bool ok, const char* what, int line) {
    if (!ok) {
        std::fprintf(stderr, "line %d: %s\n", line, what);
        std::exit(1);
    }
}
#define CHECK(x) Check((x), #x, __LINE__)

int main() {
    constexpr std::uint64_t sameAccount = 1000000000001ull;
    SlotCandidate slots[] = {
        {sameAccount, 8, false, true},  // PS4 listen host
        {sameAccount, 1, false, false}, // PC joining with the same Atlas account
        {sameAccount, 8, false, false}, // another same-account remote
        {42, 8, false, false},
    };

    CHECK(SelectSlot(slots, 4, sameAccount, true) == 0);
    CHECK(SelectSlot(slots, 4, sameAccount, false) == 1);
    slots[1].installed = true;
    CHECK(SelectSlot(slots, 4, sameAccount, false) == 2);
    slots[2].installed = true;
    CHECK(SelectSlot(slots, 4, sameAccount, false) == 1); // retry fallback
    slots[1].fake = true;
    CHECK(SelectSlot(slots, 4, sameAccount, false) == 2);
    CHECK(SelectSlot(slots, 4, 999, false) == -1);
    CHECK(ScopeMatchesClient(true, 0));
    CHECK(!ScopeMatchesClient(true, 1));
    CHECK(!ScopeMatchesClient(false, 0));
    CHECK(ScopeMatchesClient(false, 1));

    std::puts("persistence_owner tests passed");
    return 0;
}
