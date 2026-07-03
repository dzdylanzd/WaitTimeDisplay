#include "doctest.h"
#include "../src/ridereindex.h"

static RideInfo makeRide(int id) {
    RideInfo r;
    r.id = id;
    return r;
}

TEST_CASE("reindexAfterRefresh: same ride stays in the same slot") {
    int prevIds[] = {10, 20, 30};
    RideInfo newRides[] = {makeRide(10), makeRide(20), makeRide(30)};
    CHECK(reindexAfterRefresh(prevIds, 3, 1, newRides, 3) == 1);
}

TEST_CASE("reindexAfterRefresh: ride moved to a new slot after a resort") {
    int prevIds[] = {10, 20, 30};
    // Was showing ride 20 at index 1; after a wait-desc resort it's now at 0.
    RideInfo newRides[] = {makeRide(20), makeRide(30), makeRide(10)};
    CHECK(reindexAfterRefresh(prevIds, 3, 1, newRides, 3) == 0);
}

TEST_CASE("reindexAfterRefresh: ride no longer present falls back to the clamped old index") {
    int prevIds[] = {10, 20, 30};
    // Ride 20 (index 1) is gone; only 2 rides remain, so index 1 is still valid.
    RideInfo newRides[] = {makeRide(10), makeRide(30)};
    CHECK(reindexAfterRefresh(prevIds, 3, 1, newRides, 2) == 1);
}

TEST_CASE("reindexAfterRefresh: ride gone and old index now out of range falls back to 0") {
    int prevIds[] = {10, 20, 30};
    // Was showing ride 30 (index 2); it's gone and only 1 ride remains.
    RideInfo newRides[] = {makeRide(10)};
    CHECK(reindexAfterRefresh(prevIds, 3, 2, newRides, 1) == 0);
}

TEST_CASE("reindexAfterRefresh: empty new list returns 0") {
    int prevIds[] = {10, 20, 30};
    CHECK(reindexAfterRefresh(prevIds, 3, 1, nullptr, 0) == 0);
}

TEST_CASE("reindexAfterRefresh: empty previous list (first load) falls back safely") {
    RideInfo newRides[] = {makeRide(10), makeRide(20)};
    CHECK(reindexAfterRefresh(nullptr, 0, 0, newRides, 2) == 0);
}
