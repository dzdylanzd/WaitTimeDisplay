#include "doctest.h"
#include "../src/ridereindex.h"

static RideInfo makeRide(const char* id) {
    RideInfo r;
    r.id = id;
    return r;
}

TEST_CASE("reindexAfterRefresh: same ride stays in the same slot") {
    String prevIds[] = {"r10", "r20", "r30"};
    RideInfo newRides[] = {makeRide("r10"), makeRide("r20"), makeRide("r30")};
    CHECK(reindexAfterRefresh(prevIds, 3, 1, newRides, 3) == 1);
}

TEST_CASE("reindexAfterRefresh: ride moved to a new slot after a resort") {
    String prevIds[] = {"r10", "r20", "r30"};
    // Was showing ride r20 at index 1; after a wait-desc resort it's now at 0.
    RideInfo newRides[] = {makeRide("r20"), makeRide("r30"), makeRide("r10")};
    CHECK(reindexAfterRefresh(prevIds, 3, 1, newRides, 3) == 0);
}

TEST_CASE("reindexAfterRefresh: ride no longer present falls back to the clamped old index") {
    String prevIds[] = {"r10", "r20", "r30"};
    // Ride r20 (index 1) is gone; only 2 rides remain, so index 1 is still valid.
    RideInfo newRides[] = {makeRide("r10"), makeRide("r30")};
    CHECK(reindexAfterRefresh(prevIds, 3, 1, newRides, 2) == 1);
}

TEST_CASE("reindexAfterRefresh: ride gone and old index now out of range falls back to 0") {
    String prevIds[] = {"r10", "r20", "r30"};
    // Was showing ride r30 (index 2); it's gone and only 1 ride remains.
    RideInfo newRides[] = {makeRide("r10")};
    CHECK(reindexAfterRefresh(prevIds, 3, 2, newRides, 1) == 0);
}

TEST_CASE("reindexAfterRefresh: empty new list returns 0") {
    String prevIds[] = {"r10", "r20", "r30"};
    CHECK(reindexAfterRefresh(prevIds, 3, 1, nullptr, 0) == 0);
}

TEST_CASE("reindexAfterRefresh: empty previous list (first load) falls back safely") {
    RideInfo newRides[] = {makeRide("r10"), makeRide("r20")};
    CHECK(reindexAfterRefresh(nullptr, 0, 0, newRides, 2) == 0);
}
