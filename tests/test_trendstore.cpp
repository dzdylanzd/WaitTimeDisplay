#include "doctest.h"
#include "../src/trendstore.h"

// ── updateAndGetDelta ─────────────────────────────────────────────────────────

TEST_CASE("TrendStore: first sighting reports flat") {
    TrendStore ts;
    CHECK(ts.updateAndGetDelta(100, 30) == 0);
}

TEST_CASE("TrendStore: change at/above threshold reports the delta") {
    TrendStore ts;
    ts.updateAndGetDelta(100, 30);
    CHECK(ts.updateAndGetDelta(100, 35) == 5);    // exactly at threshold
    CHECK(ts.updateAndGetDelta(100, 25) == -10);  // falling
}

TEST_CASE("TrendStore: sub-threshold change reports flat but still updates") {
    TrendStore ts;
    ts.updateAndGetDelta(100, 30);
    CHECK(ts.updateAndGetDelta(100, 33) == 0);   // +3 < threshold → flat
    // The stored value advanced to 33, so a slow drift eventually registers:
    CHECK(ts.updateAndGetDelta(100, 38) == 5);   // 38 - 33
}

TEST_CASE("TrendStore: closed ride (wait < 0) neither updates nor reports") {
    TrendStore ts;
    ts.updateAndGetDelta(100, 40);
    CHECK(ts.updateAndGetDelta(100, -1) == 0);   // closed observation
    // Reopening compares against the last OPEN value, not a bogus -1:
    CHECK(ts.updateAndGetDelta(100, 40) == 0);
    CHECK(ts.updateAndGetDelta(100, 50) == 10);
}

TEST_CASE("TrendStore: invalid ride id is ignored") {
    TrendStore ts;
    CHECK(ts.updateAndGetDelta(-1, 30) == 0);
    CHECK(ts.updateAndGetDelta(-1, 90) == 0);
}

TEST_CASE("TrendStore: rides from different parks interleave independently") {
    TrendStore ts;
    ts.updateAndGetDelta(1, 10);    // park A
    ts.updateAndGetDelta(900, 60);  // park B
    // Cycle back to park A after park B — history must survive
    CHECK(ts.updateAndGetDelta(1, 25) == 15);
    CHECK(ts.updateAndGetDelta(900, 40) == -20);
}

TEST_CASE("TrendStore: clear() forgets everything") {
    TrendStore ts;
    ts.updateAndGetDelta(7, 50);
    ts.clear();
    CHECK(ts.updateAndGetDelta(7, 10) == 0);   // first sighting again
}

TEST_CASE("TrendStore: unchanged wait reports flat") {
    TrendStore ts;
    ts.updateAndGetDelta(5, 30);
    CHECK(ts.updateAndGetDelta(5, 30) == 0);
    CHECK(ts.updateAndGetDelta(5, 30) == 0);
}

TEST_CASE("TrendStore: negative delta exactly at threshold reports") {
    TrendStore ts;
    ts.updateAndGetDelta(5, 30);
    CHECK(ts.updateAndGetDelta(5, 25) == -5);   // -5 == -threshold
    ts.updateAndGetDelta(5, 25);
    CHECK(ts.updateAndGetDelta(5, 21) == 0);    // -4 is noise
}

TEST_CASE("TrendStore: over-capacity evicts oldest slots round-robin") {
    TrendStore ts;
    // Fill capacity (250) with ids 0..249
    for (int id = 0; id < 250; id++) ts.updateAndGetDelta(id, 10);
    // A new ride evicts slot 0 (id 0)
    CHECK(ts.updateAndGetDelta(1000, 20) == 0);
    // id 0 was evicted → first sighting again (flat), id 1 survives
    CHECK(ts.updateAndGetDelta(0, 60) == 0);   // re-inserted, evicting slot 1
    CHECK(ts.updateAndGetDelta(1000, 35) == 15);
}
