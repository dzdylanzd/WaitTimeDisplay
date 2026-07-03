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

TEST_CASE("mergePerParkJson: adds a new park's entry") {
    DynamicJsonDocument doc(256);
    JsonObject incoming = doc.to<JsonObject>();
    JsonArray ids = incoming.createNestedArray("5");
    ids.add(101); ids.add(102);

    String out;
    bool ok = mergePerParkJson("", incoming, {5}, out);
    CHECK(ok);
    CHECK(out.indexOf("\"5\"") >= 0);
    CHECK(out.indexOf("101") >= 0);
}

TEST_CASE("mergePerParkJson: null entry clears an existing park") {
    DynamicJsonDocument doc(256);
    JsonObject incoming = doc.to<JsonObject>();
    incoming["5"] = nullptr;

    String out;
    bool ok = mergePerParkJson("{\"5\":[101,102],\"6\":[201]}", incoming, {5, 6}, out);
    CHECK(ok);
    CHECK(out.indexOf("\"5\"") < 0);
    CHECK(out.indexOf("\"6\"") >= 0);
}

TEST_CASE("mergePerParkJson: drops parks no longer enabled") {
    String out;
    bool ok = mergePerParkJson("{\"5\":[101],\"6\":[201]}", JsonObject(), {6}, out);
    CHECK(ok);
    CHECK(out.indexOf("\"5\"") < 0);
    CHECK(out.indexOf("\"6\"") >= 0);
}

TEST_CASE("mergePerParkJson: rejects a result over the NVS budget") {
    // Build a merged object whose serialized form exceeds NVS_JSON_MAX by
    // stuffing one park with many ride ids. The source doc must be sized
    // generously enough that ArduinoJson doesn't silently truncate the
    // array before mergePerParkJson even sees it.
    DynamicJsonDocument doc(32768);
    JsonObject incoming = doc.to<JsonObject>();
    JsonArray ids = incoming.createNestedArray("5");
    for (int i = 0; i < 400; i++) ids.add(100000 + i);
    REQUIRE(ids.size() == 400);   // sanity: the source array wasn't truncated

    String out;
    bool ok = mergePerParkJson("", incoming, {5}, out);
    CHECK_FALSE(ok);
}

TEST_CASE("mergePerParkJson: empty result is a valid (empty) string") {
    String out;
    bool ok = mergePerParkJson("", JsonObject(), {}, out);
    CHECK(ok);
    CHECK(out == "");
}
