#include "doctest.h"
#include "../src/quiethours.h"

// ── inQuietWindow ─────────────────────────────────────────────────────────────

TEST_CASE("inQuietWindow: normal same-day window [start, end)") {
    // 09:00 – 17:00
    CHECK(inQuietWindow(9 * 60,      9 * 60, 17 * 60));   // start inclusive
    CHECK(inQuietWindow(12 * 60,     9 * 60, 17 * 60));
    CHECK_FALSE(inQuietWindow(17 * 60, 9 * 60, 17 * 60)); // end exclusive
    CHECK_FALSE(inQuietWindow(8 * 60 + 59, 9 * 60, 17 * 60));
    CHECK_FALSE(inQuietWindow(23 * 60, 9 * 60, 17 * 60));
}

TEST_CASE("inQuietWindow: overnight wrap (22:00 - 07:00)") {
    const int start = 22 * 60, end = 7 * 60;
    CHECK(inQuietWindow(22 * 60, start, end));       // start of the window
    CHECK(inQuietWindow(23 * 60 + 30, start, end));  // before midnight
    CHECK(inQuietWindow(0, start, end));             // midnight itself
    CHECK(inQuietWindow(6 * 60 + 59, start, end));   // last quiet minute
    CHECK_FALSE(inQuietWindow(7 * 60, start, end));  // end exclusive
    CHECK_FALSE(inQuietWindow(12 * 60, start, end)); // midday is loud
    CHECK_FALSE(inQuietWindow(21 * 60 + 59, start, end));
}

TEST_CASE("inQuietWindow: start == end is an empty window, never quiet") {
    CHECK_FALSE(inQuietWindow(0,        10 * 60, 10 * 60));
    CHECK_FALSE(inQuietWindow(10 * 60,  10 * 60, 10 * 60));
    CHECK_FALSE(inQuietWindow(23 * 60,  10 * 60, 10 * 60));
}

TEST_CASE("inQuietWindow: one-minute window") {
    CHECK(inQuietWindow(10 * 60, 10 * 60, 10 * 60 + 1));
    CHECK_FALSE(inQuietWindow(10 * 60 + 1, 10 * 60, 10 * 60 + 1));
}
