#include "doctest.h"
// Pulls in cfgserver.cpp's static (internal-linkage) helpers directly, since
// they aren't declared in cfgserver.h. Do NOT also add src/cfgserver.cpp to
// this target's source list in CMakeLists.txt -- that would duplicate every
// symbol defined here and fail to link.
#include "../src/cfgserver.cpp"

TEST_CASE("hhmmToMinutes: valid and malformed input") {
    CHECK(hhmmToMinutes("07:30", -1) == 450);
    CHECK(hhmmToMinutes("00:00", -1) == 0);
    CHECK(hhmmToMinutes("23:59", -1) == 1439);
    CHECK(hhmmToMinutes("abc", 99) == 99);
    CHECK(hhmmToMinutes("25:00", 99) == 99);   // hour out of range
    CHECK(hhmmToMinutes("10:60", 99) == 99);   // minute out of range
    CHECK(hhmmToMinutes(nullptr, 99) == 99);
    CHECK(hhmmToMinutes("", 99) == 99);
}

TEST_CASE("parseHexColor: valid and malformed input") {
    CHECK(parseHexColor("#ff8800", 0) == 0xFF8800u);
    CHECK(parseHexColor("ff8800", 0) == 0xFF8800u);     // no # prefix
    CHECK(parseHexColor("#000000", 1) == 0x000000u);
    CHECK(parseHexColor("#fff", 42) == 42u);            // wrong length
    CHECK(parseHexColor("#gggggg", 42) == 42u);         // non-hex chars
    CHECK(parseHexColor(nullptr, 42) == 42u);
}

TEST_CASE("hexColor: round-trips through parseHexColor") {
    CHECK(hexColor(0xFF8800) == "#ff8800");
    CHECK(hexColor(0x000000) == "#000000");
    CHECK(parseHexColor(hexColor(0xC89E20).c_str(), 0) == 0xC89E20u);
}

TEST_CASE("jsonEscape: escapes control characters") {
    CHECK(jsonEscape("plain") == "plain");
    CHECK(jsonEscape("a\"b") == "a\\\"b");
    CHECK(jsonEscape("a\\b") == "a\\\\b");
    CHECK(jsonEscape("a\nb") == "a\\nb");
    CHECK(jsonEscape("a\rb") == "a\\rb");
    CHECK(jsonEscape("a\tb") == "a\\tb");
}

// (mergePerParkJson was replaced by ConfigManager::applyRideSelections —
//  its per-park merge/cleanup/limit behaviour is covered in
//  test_configmanager.cpp.)
