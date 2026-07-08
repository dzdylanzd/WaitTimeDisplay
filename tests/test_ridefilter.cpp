#include "doctest.h"
#include "../src/ridefilter.h"

static RideInfo mkRide(const char* id, const char* name, int wait, bool open,
                       bool fav = false) {
    RideInfo r;
    r.id = id; r.name = name; r.waitTime = wait;
    r.status = open ? RideStatus::Operating : RideStatus::Closed;
    r.favorite = fav;
    return r;
}

static RideInfo mkStatus(const char* id, RideStatus st, int wait = -1) {
    RideInfo r;
    r.id = id; r.name = id; r.status = st; r.waitTime = wait;
    return r;
}

static RideInfo mkShow(const char* id, int nextShowMin, bool fav = false) {
    RideInfo r;
    r.id = id; r.name = id; r.kind = EntityKind::Show;
    r.status = RideStatus::Operating; r.nextShowMin = (int16_t)nextShowMin;
    r.favorite = fav;
    return r;
}

// ── filtering ─────────────────────────────────────────────────────────────────

TEST_CASE("applyDisplayOptions: defaults leave the list untouched") {
    RideInfo rides[3] = { mkRide("1", "A", 60, true), mkRide("2", "B", 5, true),
                          mkRide("3", "C", -1, false) };
    int count = 3;
    RideDisplayOptions opt;   // API order, favorites-first with no favorites
    applyDisplayOptions(rides, count, opt);
    CHECK(count == 3);
    CHECK(rides[0].id == "1");
    CHECK(rides[1].id == "2");
    CHECK(rides[2].id == "3");
}

TEST_CASE("applyDisplayOptions: skipClosed drops closed rides") {
    RideInfo rides[3] = { mkRide("1", "A", 60, true), mkRide("2", "B", -1, false),
                          mkRide("3", "C", 10, true) };
    int count = 3;
    RideDisplayOptions opt; opt.skipClosed = true;
    applyDisplayOptions(rides, count, opt);
    CHECK(count == 2);
    CHECK(rides[0].id == "1");
    CHECK(rides[1].id == "3");
}

TEST_CASE("applyDisplayOptions: skipClosed drops DOWN and REFURBISHMENT too") {
    // User decision: skip-closed means "only things I can experience now" —
    // a broken-down ride is hidden, not surfaced.
    RideInfo rides[4] = { mkStatus("1", RideStatus::Operating, 30),
                          mkStatus("2", RideStatus::Down, 0),
                          mkStatus("3", RideStatus::Refurbishment),
                          mkStatus("4", RideStatus::Closed) };
    int count = 4;
    RideDisplayOptions opt; opt.skipClosed = true;
    applyDisplayOptions(rides, count, opt);
    CHECK(count == 1);
    CHECK(rides[0].id == "1");
}

TEST_CASE("applyDisplayOptions: skipClosed keeps shows with a showtime left, drops finished ones") {
    RideInfo rides[3] = { mkShow("1", 900), mkShow("2", -1),
                          mkRide("3", "C", 10, true) };
    int count = 3;
    RideDisplayOptions opt; opt.skipClosed = true;
    applyDisplayOptions(rides, count, opt);
    CHECK(count == 2);
    CHECK(rides[0].id == "1");
    CHECK(rides[1].id == "3");
}

TEST_CASE("applyDisplayOptions: minWait hides only short OPEN waits") {
    RideInfo rides[4] = { mkRide("1", "A", 60, true), mkRide("2", "B", 5, true),
                          mkRide("3", "C", -1, true),     // open, wait unknown → kept
                          mkRide("4", "D", 0, false) };   // closed → kept (skipClosed off)
    int count = 4;
    RideDisplayOptions opt; opt.minWaitMinutes = 15;
    applyDisplayOptions(rides, count, opt);
    CHECK(count == 3);
    CHECK(rides[0].id == "1");
    CHECK(rides[1].id == "3");
    CHECK(rides[2].id == "4");
}

TEST_CASE("applyDisplayOptions: minWait never drops shows (they have no wait)") {
    RideInfo rides[2] = { mkShow("1", 700), mkRide("2", "B", 5, true) };
    int count = 2;
    RideDisplayOptions opt; opt.minWaitMinutes = 15;
    applyDisplayOptions(rides, count, opt);
    CHECK(count == 1);
    CHECK(rides[0].id == "1");
}

TEST_CASE("applyDisplayOptions: reverts when the filter would empty the list") {
    RideInfo rides[2] = { mkRide("1", "A", -1, false), mkRide("2", "B", -1, false) };
    int count = 2;
    RideDisplayOptions opt; opt.skipClosed = true;
    applyDisplayOptions(rides, count, opt);
    CHECK(count == 2);   // all closed — list kept so the CLOSED screen works
}

TEST_CASE("applyDisplayOptions: all-DOWN park also reverts instead of emptying") {
    RideInfo rides[2] = { mkStatus("1", RideStatus::Down, 0),
                          mkStatus("2", RideStatus::Down, 0) };
    int count = 2;
    RideDisplayOptions opt; opt.skipClosed = true;
    applyDisplayOptions(rides, count, opt);
    CHECK(count == 2);
}

// ── sorting ───────────────────────────────────────────────────────────────────

TEST_CASE("applyDisplayOptions: wait-desc puts longest first, closed last") {
    RideInfo rides[4] = { mkRide("1", "A", 10, true), mkRide("2", "B", -1, false),
                          mkRide("3", "C", 45, true), mkRide("4", "D", -1, true) };
    int count = 4;
    RideDisplayOptions opt;
    opt.sortMode = SORT_MODE_WAIT_DESC;
    opt.favoritesFirst = false;
    applyDisplayOptions(rides, count, opt);
    CHECK(rides[0].id == "3");   // 45 min
    CHECK(rides[1].id == "1");   // 10 min
    CHECK(rides[2].id == "4");   // open, unknown wait
    CHECK(rides[3].id == "2");   // closed
}

TEST_CASE("applyDisplayOptions: wait-desc ranks shows between waits and closed, DOWN last") {
    RideInfo rides[4] = { mkStatus("1", RideStatus::Down, 0),
                          mkShow("2", 1000),   // later show
                          mkShow("3", 600),    // sooner show first
                          mkRide("4", "D", 5, true) };
    int count = 4;
    RideDisplayOptions opt;
    opt.sortMode = SORT_MODE_WAIT_DESC;
    opt.favoritesFirst = false;
    applyDisplayOptions(rides, count, opt);
    CHECK(rides[0].id == "4");   // real wait beats shows
    CHECK(rides[1].id == "3");   // soonest show
    CHECK(rides[2].id == "2");
    CHECK(rides[3].id == "1");   // down ranks with closed, at the bottom
}

TEST_CASE("applyDisplayOptions: wait-desc is stable for equal waits") {
    RideInfo rides[3] = { mkRide("1", "A", 30, true), mkRide("2", "B", 30, true),
                          mkRide("3", "C", 30, true) };
    int count = 3;
    RideDisplayOptions opt;
    opt.sortMode = SORT_MODE_WAIT_DESC;
    applyDisplayOptions(rides, count, opt);
    CHECK(rides[0].id == "1");
    CHECK(rides[1].id == "2");
    CHECK(rides[2].id == "3");
}

TEST_CASE("applyDisplayOptions: favoritesFirst partitions, keeping API order") {
    RideInfo rides[4] = { mkRide("1", "A", 10, true), mkRide("2", "B", 20, true, true),
                          mkRide("3", "C", 30, true), mkRide("4", "D", 40, true, true) };
    int count = 4;
    RideDisplayOptions opt;   // favoritesFirst defaults true, API order
    applyDisplayOptions(rides, count, opt);
    CHECK(rides[0].id == "2");
    CHECK(rides[1].id == "4");
    CHECK(rides[2].id == "1");
    CHECK(rides[3].id == "3");
}

TEST_CASE("applyDisplayOptions: favorites first, wait-desc within partitions") {
    RideInfo rides[4] = { mkRide("1", "A", 90, true), mkRide("2", "B", 10, true, true),
                          mkRide("3", "C", 50, true, true), mkRide("4", "D", 20, true) };
    int count = 4;
    RideDisplayOptions opt;
    opt.sortMode = SORT_MODE_WAIT_DESC;
    applyDisplayOptions(rides, count, opt);
    CHECK(rides[0].id == "3");   // favorite, 50
    CHECK(rides[1].id == "2");   // favorite, 10
    CHECK(rides[2].id == "1");   // 90
    CHECK(rides[3].id == "4");   // 20
}

TEST_CASE("applyDisplayOptions: empty list is a no-op") {
    RideInfo rides[1];
    int count = 0;
    RideDisplayOptions opt; opt.skipClosed = true; opt.sortMode = SORT_MODE_WAIT_DESC;
    applyDisplayOptions(rides, count, opt);
    CHECK(count == 0);
}

TEST_CASE("applyDisplayOptions: single ride passes through every option") {
    RideInfo rides[1] = { mkRide("1", "A", 20, true, true) };
    int count = 1;
    RideDisplayOptions opt;
    opt.sortMode = SORT_MODE_WAIT_DESC; opt.skipClosed = true; opt.minWaitMinutes = 5;
    applyDisplayOptions(rides, count, opt);
    CHECK(count == 1);
    CHECK(rides[0].id == "1");
}

TEST_CASE("applyDisplayOptions: wait equal to minWait is kept (>= threshold)") {
    RideInfo rides[2] = { mkRide("1", "A", 15, true), mkRide("2", "B", 14, true) };
    int count = 2;
    RideDisplayOptions opt; opt.minWaitMinutes = 15;
    applyDisplayOptions(rides, count, opt);
    CHECK(count == 1);
    CHECK(rides[0].id == "1");
}

TEST_CASE("applyDisplayOptions: closed favorite is dropped by skipClosed") {
    // Filtering runs before the favorites-first sort — a closed favorite
    // doesn't sneak back in.
    RideInfo rides[3] = { mkRide("1", "A", 30, true), mkRide("2", "B", -1, false, true),
                          mkRide("3", "C", 10, true) };
    int count = 3;
    RideDisplayOptions opt; opt.skipClosed = true;
    applyDisplayOptions(rides, count, opt);
    CHECK(count == 2);
    CHECK(rides[0].id == "1");
    CHECK(rides[1].id == "3");
}

TEST_CASE("applyDisplayOptions: filter and sort combine") {
    RideInfo rides[5] = { mkRide("1", "A", 5, true), mkRide("2", "B", -1, false),
                          mkRide("3", "C", 25, true, true), mkRide("4", "D", 70, true),
                          mkRide("5", "E", 40, true) };
    int count = 5;
    RideDisplayOptions opt;
    opt.sortMode = SORT_MODE_WAIT_DESC;
    opt.skipClosed = true;
    opt.minWaitMinutes = 10;
    applyDisplayOptions(rides, count, opt);
    CHECK(count == 3);           // A (short) and B (closed) dropped
    CHECK(rides[0].id == "3");   // favorite leads
    CHECK(rides[1].id == "4");   // 70
    CHECK(rides[2].id == "5");   // 40
}
