#include "doctest.h"
#include "../src/waitlevel.h"

// ── default thresholds (15/30/45) ─────────────────────────────────────────────

TEST_CASE("pickWaitLevel: default thresholds and boundaries") {
    CHECK(pickWaitLevel(0,  true) == WaitLevel::Green);
    CHECK(pickWaitLevel(15, true) == WaitLevel::Green);   // inclusive
    CHECK(pickWaitLevel(16, true) == WaitLevel::Amber);
    CHECK(pickWaitLevel(30, true) == WaitLevel::Amber);
    CHECK(pickWaitLevel(31, true) == WaitLevel::Orange);
    CHECK(pickWaitLevel(45, true) == WaitLevel::Orange);
    CHECK(pickWaitLevel(46, true) == WaitLevel::Red);
    CHECK(pickWaitLevel(240, true) == WaitLevel::Red);
}

TEST_CASE("pickWaitLevel: closed wins regardless of wait time") {
    CHECK(pickWaitLevel(0,   false) == WaitLevel::Closed);
    CHECK(pickWaitLevel(100, false) == WaitLevel::Closed);
    CHECK(pickWaitLevel(100, false, 5, 10, 20) == WaitLevel::Closed);
}

// ── custom thresholds ─────────────────────────────────────────────────────────

TEST_CASE("pickWaitLevel: custom thresholds move every boundary") {
    CHECK(pickWaitLevel(5,  true, 5, 10, 20) == WaitLevel::Green);
    CHECK(pickWaitLevel(6,  true, 5, 10, 20) == WaitLevel::Amber);
    CHECK(pickWaitLevel(10, true, 5, 10, 20) == WaitLevel::Amber);
    CHECK(pickWaitLevel(11, true, 5, 10, 20) == WaitLevel::Orange);
    CHECK(pickWaitLevel(20, true, 5, 10, 20) == WaitLevel::Orange);
    CHECK(pickWaitLevel(21, true, 5, 10, 20) == WaitLevel::Red);
}

TEST_CASE("pickWaitLevel: high custom thresholds keep long waits green") {
    CHECK(pickWaitLevel(60,  true, 60, 90, 120) == WaitLevel::Green);
    CHECK(pickWaitLevel(90,  true, 60, 90, 120) == WaitLevel::Amber);
    CHECK(pickWaitLevel(120, true, 60, 90, 120) == WaitLevel::Orange);
    CHECK(pickWaitLevel(121, true, 60, 90, 120) == WaitLevel::Red);
}

// ── status overload (themeparks.wiki RideStatus) ──────────────────────────────

TEST_CASE("pickWaitLevel(status): Operating uses the normal buckets") {
    CHECK(pickWaitLevel(10, RideStatus::Operating) == WaitLevel::Green);
    CHECK(pickWaitLevel(25, RideStatus::Operating) == WaitLevel::Amber);
    CHECK(pickWaitLevel(40, RideStatus::Operating) == WaitLevel::Orange);
    CHECK(pickWaitLevel(60, RideStatus::Operating) == WaitLevel::Red);
    CHECK(pickWaitLevel(25, RideStatus::Operating, 5, 10, 20) == WaitLevel::Red);
}

TEST_CASE("pickWaitLevel(status): DOWN is always red, whatever the wait") {
    CHECK(pickWaitLevel(0,  RideStatus::Down) == WaitLevel::Red);
    CHECK(pickWaitLevel(-1, RideStatus::Down) == WaitLevel::Red);
    CHECK(pickWaitLevel(5,  RideStatus::Down, 60, 90, 120) == WaitLevel::Red);
}

TEST_CASE("pickWaitLevel(status): Closed and Refurbishment map to Closed") {
    CHECK(pickWaitLevel(0,   RideStatus::Closed)        == WaitLevel::Closed);
    CHECK(pickWaitLevel(100, RideStatus::Closed)        == WaitLevel::Closed);
    CHECK(pickWaitLevel(0,   RideStatus::Refurbishment) == WaitLevel::Closed);
}
