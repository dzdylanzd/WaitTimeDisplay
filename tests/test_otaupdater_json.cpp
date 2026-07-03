#include "doctest.h"
#include "../src/otaupdater.h"

static const char* kSampleRelease = R"json(
{
  "tag_name": "v1.2.0",
  "assets": [
    {"name": "queuewatch.elf", "browser_download_url": "https://example.com/queuewatch.elf"},
    {"name": "firmware.bin", "browser_download_url": "https://example.com/firmware.bin"}
  ]
}
)json";

TEST_CASE("extractLatestRelease: picks the .bin asset by name, not index") {
    DynamicJsonDocument doc(1024);
    REQUIRE_FALSE(deserializeJson(doc, kSampleRelease));

    String version, url;
    bool ok = extractLatestRelease(doc.as<JsonVariantConst>(), version, url);
    CHECK(ok);
    CHECK(version == "v1.2.0");
    CHECK(url == "https://example.com/firmware.bin");
}

TEST_CASE("extractLatestRelease: fails when there's no .bin asset") {
    static const char* json = R"json(
    {"tag_name": "v1.2.0", "assets": [{"name": "notes.txt", "browser_download_url": "https://example.com/notes.txt"}]}
    )json";
    DynamicJsonDocument doc(1024);
    REQUIRE_FALSE(deserializeJson(doc, json));

    String version, url;
    CHECK_FALSE(extractLatestRelease(doc.as<JsonVariantConst>(), version, url));
}

TEST_CASE("extractLatestRelease: fails when tag_name is missing") {
    static const char* json = R"json(
    {"assets": [{"name": "firmware.bin", "browser_download_url": "https://example.com/firmware.bin"}]}
    )json";
    DynamicJsonDocument doc(1024);
    REQUIRE_FALSE(deserializeJson(doc, json));

    String version, url;
    CHECK_FALSE(extractLatestRelease(doc.as<JsonVariantConst>(), version, url));
}

TEST_CASE("extractLatestRelease: fails when assets array is empty or missing") {
    static const char* json = R"json({"tag_name": "v1.2.0", "assets": []})json";
    DynamicJsonDocument doc(1024);
    REQUIRE_FALSE(deserializeJson(doc, json));

    String version, url;
    CHECK_FALSE(extractLatestRelease(doc.as<JsonVariantConst>(), version, url));
}
