#include "doctest.h"
#include "../src/button.h"

// All tests drive the pure update(pressed, now) state machine directly.

TEST_CASE("Button: bounce shorter than debounce is ignored") {
    Button b;
    CHECK(b.update(true,  0)  == ButtonEvent::None);
    CHECK(b.update(true,  10) == ButtonEvent::None);
    CHECK(b.update(false, 20) == ButtonEvent::None);   // released before 30 ms
    CHECK(b.update(false, 100) == ButtonEvent::None);  // never became stable
}

TEST_CASE("Button: press then release yields Short") {
    Button b;
    b.update(true, 0);
    CHECK(b.update(true, 40)  == ButtonEvent::None);   // debounced press
    b.update(false, 100);                              // raw release
    CHECK(b.update(false, 140) == ButtonEvent::Short); // debounced release
}

TEST_CASE("Button: hold fires Long once, before release") {
    Button b;
    b.update(true, 0);
    b.update(true, 40);                                 // stable press at t=0
    CHECK(b.update(true, 600) == ButtonEvent::None);    // not yet
    CHECK(b.update(true, 700) == ButtonEvent::Long);    // threshold reached
    CHECK(b.update(true, 900) == ButtonEvent::None);    // no repeat
    b.update(false, 1000);
    CHECK(b.update(false, 1040) == ButtonEvent::None);  // release swallowed
}

TEST_CASE("Button: 10 s hold fires HoldWarning once (after Long)") {
    Button b;
    b.update(true, 0); b.update(true, 40);                    // stable press
    CHECK(b.update(true, 800)   == ButtonEvent::Long);        // 700 ms
    CHECK(b.update(true, 9999)  == ButtonEvent::None);
    CHECK(b.update(true, 10000) == ButtonEvent::HoldWarning); // 10 s
    CHECK(b.update(true, 12000) == ButtonEvent::None);        // fires once
}

TEST_CASE("Button: release during the warning window yields HoldCancel") {
    Button b;
    b.update(true, 0); b.update(true, 40);
    b.update(true, 800);                                       // Long
    b.update(true, 10000);                                     // HoldWarning
    b.update(false, 15000);                                    // raw release
    CHECK(b.update(false, 15040) == ButtonEvent::HoldCancel);  // debounced
    // A fresh press afterwards behaves normally
    b.update(true, 16000); b.update(true, 16040);
    b.update(false, 16100);
    CHECK(b.update(false, 16140) == ButtonEvent::Short);
}

TEST_CASE("Button: 20 s hold fires HoldReset; the release is swallowed") {
    Button b;
    b.update(true, 0); b.update(true, 40);
    b.update(true, 800);                                       // Long
    b.update(true, 10000);                                     // HoldWarning
    CHECK(b.update(true, 20000) == ButtonEvent::HoldReset);
    CHECK(b.update(true, 25000) == ButtonEvent::None);         // fires once
    b.update(false, 26000);
    CHECK(b.update(false, 26040) == ButtonEvent::None);        // no Cancel/Short
}

TEST_CASE("Button: polling that skips straight past 20 s fires HoldReset") {
    Button b;
    b.update(true, 0); b.update(true, 40);
    CHECK(b.update(true, 30000) == ButtonEvent::HoldReset);    // no warning first
    b.update(false, 31000);
    CHECK(b.update(false, 31040) == ButtonEvent::None);
}

TEST_CASE("Button: a release bounce during a hold does not cancel the hold") {
    Button b;
    b.update(true, 0); b.update(true, 40);          // stable press at t=0
    // Contact bounce: raw release for 10 ms in the middle of the hold
    b.update(false, 5000);
    b.update(true, 5010);
    CHECK(b.update(true, 5050)  == ButtonEvent::None);
    // Hold timing still measured from the original press edge
    CHECK(b.update(true, 10000) == ButtonEvent::HoldWarning);
}

TEST_CASE("Button: two quick short presses both register") {
    Button b;
    b.update(true, 0);    b.update(true, 40);
    b.update(false, 100);
    CHECK(b.update(false, 140) == ButtonEvent::Short);
    b.update(true, 300);  b.update(true, 340);
    b.update(false, 400);
    CHECK(b.update(false, 440) == ButtonEvent::Short);
}

TEST_CASE("Button: back-to-back presses each yield an event") {
    Button b;
    // First: short press
    b.update(true, 0);   b.update(true, 40);
    b.update(false, 100);
    CHECK(b.update(false, 140) == ButtonEvent::Short);
    // Second: long press
    b.update(true, 200); b.update(true, 240);
    CHECK(b.update(true, 950) == ButtonEvent::Long);    // 950 - 200 >= 700
    b.update(false, 1000);
    CHECK(b.update(false, 1040) == ButtonEvent::None);
    // Third: another short press still works
    b.update(true, 1100); b.update(true, 1140);
    b.update(false, 1200);
    CHECK(b.update(false, 1240) == ButtonEvent::Short);
}
