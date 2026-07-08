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

TEST_CASE("countryForDestination: maps destinations to countries") {
    // The user's parks
    CHECK(String(countryForDestination("Parc Asterix")) == "France");
    CHECK(String(countryForDestination("Disneyland Paris")) == "France");
    CHECK(String(countryForDestination("Tokyo Disney Resort")) == "Japan");
    CHECK(String(countryForDestination("Universal Orlando Resort")) == "USA");
    CHECK(String(countryForDestination("Walt Disney World\xC2\xAE Resort")) == "USA");
    CHECK(String(countryForDestination("Phantasialand")) == "Germany");
    CHECK(String(countryForDestination("Attractiepark Toverland")) == "Netherlands");
    CHECK(String(countryForDestination("Efteling")) == "Netherlands");
    // Multi-country chains resolve to the right country, not the chain's home
    CHECK(String(countryForDestination("Six Flags Magic Mountain")) == "USA");
    CHECK(String(countryForDestination("Six Flags Mexico")) == "Mexico");
    CHECK(String(countryForDestination("Six Flags Qiddiya City")) == "Saudi Arabia");
    CHECK(String(countryForDestination("Universal Studios Japan")) == "Japan");
    CHECK(String(countryForDestination("Universal Studios Singapore")) == "Singapore");
    CHECK(String(countryForDestination("Shanghai Disney Resort")) == "China");
    CHECK(String(countryForDestination("Hong Kong Disneyland Parks")) == "Hong Kong");
    CHECK(String(countryForDestination("LEGOLAND Deutschland")) == "Germany");
    CHECK(String(countryForDestination("LEGOLAND\xC2\xAE Korea")) == "South Korea");
    CHECK(String(countryForDestination("LEGOLAND California")) == "USA");
    // Australia (Gold Coast group must win over the SeaWorld keyword)
    CHECK(String(countryForDestination("Sea World Gold Coast")) == "Australia");
    CHECK(String(countryForDestination("Warner Bros. Movie World")) == "Australia");
    // Oaxtepec (Mexico) must win over the Hurricane Harbor US catch-all
    CHECK(String(countryForDestination("Hurricane Harbor Oaxtepec")) == "Mexico");
    // Unknown falls through to Other
    CHECK(String(countryForDestination("Totally New Park")) == "Other");
}
