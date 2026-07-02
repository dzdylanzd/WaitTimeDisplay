#include "doctest.h"
#include "../src/configmanager.h"

// ── parseEnabledParks ─────────────────────────────────────────────────────────

TEST_CASE("parseEnabledParks: valid JSON returns parks") {
    ConfigManager mgr;
    std::vector<int>    ids;
    std::vector<String> names;

    bool ok = mgr.parseEnabledParks(
        R"([{"id":1,"name":"Magic Kingdom"},{"id":7,"name":"EPCOT"}])",
        ids, names);

    CHECK(ok);
    CHECK(ids.size()   == 2);
    CHECK(names.size() == 2);
    CHECK(ids[0]   == 1);
    CHECK(names[0] == "Magic Kingdom");
    CHECK(ids[1]   == 7);
    CHECK(names[1] == "EPCOT");
}

TEST_CASE("parseEnabledParks: empty array returns false") {
    ConfigManager mgr;
    std::vector<int> ids; std::vector<String> names;
    CHECK_FALSE(mgr.parseEnabledParks("[]", ids, names));
    CHECK(ids.empty());
}

TEST_CASE("parseEnabledParks: malformed JSON returns false") {
    ConfigManager mgr;
    std::vector<int> ids; std::vector<String> names;
    CHECK_FALSE(mgr.parseEnabledParks("{not valid json", ids, names));
}

TEST_CASE("parseEnabledParks: entries with invalid id <= 0 are skipped") {
    ConfigManager mgr;
    std::vector<int> ids; std::vector<String> names;
    bool ok = mgr.parseEnabledParks(
        R"([{"id":-1,"name":"Bad"},{"id":0,"name":"Zero"},{"id":5,"name":"Good"}])",
        ids, names);
    CHECK(ok);
    CHECK(ids.size() == 1);
    CHECK(ids[0] == 5);
}

// ── isRideEnabled ─────────────────────────────────────────────────────────────

TEST_CASE("isRideEnabled: no filter — all rides enabled") {
    ConfigManager mgr;
    // rideFiltersJson is empty by default
    CHECK(mgr.isRideEnabled(1, 100));
    CHECK(mgr.isRideEnabled(1, 999));
}

TEST_CASE("isRideEnabled: park not in filter — all enabled") {
    ConfigManager mgr;
    mgr.saveRideFilters(R"({"5":[10,20,30]})");
    // Park 99 has no filter entry → all its rides enabled
    CHECK(mgr.isRideEnabled(99, 1));
}

TEST_CASE("isRideEnabled: ride in filter list — enabled") {
    ConfigManager mgr;
    mgr.saveRideFilters(R"({"1":[10,20,30]})");
    CHECK(mgr.isRideEnabled(1, 10));
    CHECK(mgr.isRideEnabled(1, 20));
    CHECK(mgr.isRideEnabled(1, 30));
}

TEST_CASE("isRideEnabled: ride NOT in filter list — disabled") {
    ConfigManager mgr;
    mgr.saveRideFilters(R"({"1":[10,20]})");
    CHECK_FALSE(mgr.isRideEnabled(1, 99));
}

// ── saveRideFilters / cache invalidation ──────────────────────────────────────

TEST_CASE("isRideEnabled: cache re-built after saveRideFilters") {
    ConfigManager mgr;
    mgr.saveRideFilters(R"({"2":[5,6]})");
    CHECK(mgr.isRideEnabled(2, 5));
    CHECK_FALSE(mgr.isRideEnabled(2, 9));

    // Update filter — cache must be invalidated
    mgr.saveRideFilters(R"({"2":[9]})");
    CHECK(mgr.isRideEnabled(2, 9));
    CHECK_FALSE(mgr.isRideEnabled(2, 5));
}

// ── saveEnabledParks round-trip ───────────────────────────────────────────────

TEST_CASE("saveEnabledParks: round-trips via parseEnabledParks") {
    ConfigManager mgr;
    mgr.saveEnabledParks(R"([{"id":3,"name":"Disneyland"},{"id":8,"name":"Hollywood Studios"}])");

    const RuntimeConfig& cfg = mgr.getConfig();
    REQUIRE(cfg.enabledParkIds.size() == 2);
    CHECK(cfg.enabledParkIds[0] == 3);
    CHECK(cfg.enabledParkNames[0] == "Disneyland");
    CHECK(cfg.enabledParkIds[1] == 8);
}

TEST_CASE("hasEnabledParks: false when empty, true after setting") {
    ConfigManager mgr;
    CHECK_FALSE(mgr.hasEnabledParks());
    mgr.saveEnabledParks(R"([{"id":1,"name":"Park"}])");
    CHECK(mgr.hasEnabledParks());
}

// ── saveTimings ───────────────────────────────────────────────────────────────

TEST_CASE("saveTimings: values reflected in getConfig immediately") {
    ConfigManager mgr;
    mgr.saveTimings(120000, 15000, 25000, 45000);
    const auto& cfg = mgr.getConfig();
    CHECK(cfg.apiRefreshInterval    == 120000);
    CHECK(cfg.rotateInterval        == 15000);
    CHECK(cfg.closedParkDisplayTime == 25000);
    CHECK(cfg.timeUpdateInterval    == 45000);
}

// ── saveDisplaySettings ───────────────────────────────────────────────────────

TEST_CASE("saveDisplaySettings: values reflected in getConfig") {
    ConfigManager mgr;
    mgr.saveDisplaySettings(40, true, 21 * 60, 6 * 60 + 30, 10, false, true);
    const auto& cfg = mgr.getConfig();
    CHECK(cfg.brightness        == 40);
    CHECK(cfg.quietHoursEnabled == true);
    CHECK(cfg.quietStartMin     == 21 * 60);
    CHECK(cfg.quietEndMin       == 6 * 60 + 30);
    CHECK(cfg.quietBrightness   == 10);
    CHECK(cfg.ledEnabled        == false);
    CHECK(cfg.flipScreen        == true);
}

// ── saveRideOptions ───────────────────────────────────────────────────────────

TEST_CASE("saveRideOptions: values reflected in getConfig") {
    ConfigManager mgr;
    mgr.saveRideOptions(SORT_MODE_WAIT_DESC, false, true, 15);
    const auto& cfg = mgr.getConfig();
    CHECK(cfg.sortMode        == SORT_MODE_WAIT_DESC);
    CHECK(cfg.favoritesFirst  == false);
    CHECK(cfg.skipClosedRides == true);
    CHECK(cfg.minWaitMinutes  == 15);
}

// ── isRideFavorite ────────────────────────────────────────────────────────────

TEST_CASE("isRideFavorite: empty favorites — nothing is a favorite") {
    ConfigManager mgr;
    CHECK_FALSE(mgr.isRideFavorite(1, 100));
}

TEST_CASE("isRideFavorite: only listed rides are favorites") {
    ConfigManager mgr;
    mgr.saveRideFavorites(R"({"5":[10,20]})");
    CHECK(mgr.isRideFavorite(5, 10));
    CHECK(mgr.isRideFavorite(5, 20));
    CHECK_FALSE(mgr.isRideFavorite(5, 30));
    // Unlike the ride filter, a missing park means NOT favorite
    CHECK_FALSE(mgr.isRideFavorite(99, 10));
}

TEST_CASE("isRideFavorite: cache re-built after saveRideFavorites") {
    ConfigManager mgr;
    mgr.saveRideFavorites(R"({"2":[7]})");
    CHECK(mgr.isRideFavorite(2, 7));
    mgr.saveRideFavorites(R"({"2":[8]})");
    CHECK(mgr.isRideFavorite(2, 8));
    CHECK_FALSE(mgr.isRideFavorite(2, 7));
}

TEST_CASE("isRideFavorite: malformed favorites JSON — nothing is a favorite") {
    ConfigManager mgr;
    mgr.saveRideFavorites("{not valid json");
    CHECK_FALSE(mgr.isRideFavorite(1, 10));
}

TEST_CASE("isRideEnabled: malformed filter JSON — everything stays enabled") {
    ConfigManager mgr;
    mgr.saveRideFilters("{broken");
    CHECK(mgr.isRideEnabled(1, 10));
    CHECK(mgr.isRideEnabled(99, 42));
}

TEST_CASE("parseEnabledParks: JSON object instead of array returns false") {
    ConfigManager mgr;
    std::vector<int> ids; std::vector<String> names;
    CHECK_FALSE(mgr.parseEnabledParks(R"({"id":1,"name":"Park"})", ids, names));
    CHECK(ids.empty());
}

// ── load() persistence round-trip ─────────────────────────────────────────────

TEST_CASE("load: saved settings survive a reload from Preferences") {
    ConfigManager mgr;
    mgr.saveTimings(60000, 5000, 15000, 30000);
    mgr.saveDisplaySettings(55, true, 20 * 60, 8 * 60, 25, false, true);
    mgr.saveRideOptions(SORT_MODE_WAIT_DESC, false, true, 10);
    mgr.saveEnabledParks(R"([{"id":4,"name":"Paris"}])");
    mgr.saveRideFilters(R"({"4":[1,2]})");
    mgr.saveRideFavorites(R"({"4":[2]})");

    mgr.load();   // re-read everything from the (mock) NVS

    const auto& cfg = mgr.getConfig();
    CHECK(cfg.apiRefreshInterval == 60000);
    CHECK(cfg.rotateInterval     == 5000);
    CHECK(cfg.brightness         == 55);
    CHECK(cfg.quietHoursEnabled  == true);
    CHECK(cfg.quietStartMin      == 20 * 60);
    CHECK(cfg.quietEndMin        == 8 * 60);
    CHECK(cfg.quietBrightness    == 25);
    CHECK(cfg.ledEnabled         == false);
    CHECK(cfg.flipScreen         == true);
    CHECK(cfg.sortMode           == SORT_MODE_WAIT_DESC);
    CHECK(cfg.favoritesFirst     == false);
    CHECK(cfg.skipClosedRides    == true);
    CHECK(cfg.minWaitMinutes     == 10);
    REQUIRE(cfg.enabledParkIds.size() == 1);
    CHECK(cfg.enabledParkIds[0]  == 4);
    CHECK(mgr.isRideEnabled(4, 1));
    CHECK_FALSE(mgr.isRideEnabled(4, 3));
    CHECK(mgr.isRideFavorite(4, 2));
    CHECK_FALSE(mgr.isRideFavorite(4, 1));
}

// ── factoryReset ──────────────────────────────────────────────────────────────

TEST_CASE("factoryReset: wipes parks/filters/timings back to defaults") {
    ConfigManager mgr;
    mgr.saveTimings(120000, 15000, 25000, 45000);
    mgr.saveEnabledParks(R"([{"id":1,"name":"Park"}])");
    mgr.saveRideFilters(R"({"1":[10,20]})");
    mgr.saveDisplaySettings(40, true, 21 * 60, 6 * 60, 10, false, true);
    mgr.saveRideOptions(SORT_MODE_WAIT_DESC, false, true, 15);
    mgr.saveRideFavorites(R"({"1":[10]})");
    REQUIRE(mgr.hasEnabledParks());
    REQUIRE_FALSE(mgr.isRideEnabled(1, 99));
    REQUIRE(mgr.isRideFavorite(1, 10));

    mgr.factoryReset();

    const auto& cfg = mgr.getConfig();
    CHECK_FALSE(mgr.hasEnabledParks());
    CHECK(cfg.rideFiltersJson.length() == 0);
    CHECK(cfg.apiRefreshInterval    == DEFAULT_API_REFRESH_INTERVAL);
    CHECK(cfg.rotateInterval        == DEFAULT_ROTATE_INTERVAL);
    CHECK(cfg.closedParkDisplayTime == DEFAULT_CLOSED_PARK_DISPLAY_TIME);
    CHECK(cfg.timeUpdateInterval    == DEFAULT_TIME_UPDATE_INTERVAL);
    CHECK(mgr.isRideEnabled(1, 99));  // filter gone — everything enabled again
    CHECK(cfg.brightness        == 100);
    CHECK(cfg.quietHoursEnabled == false);
    CHECK(cfg.ledEnabled        == true);
    CHECK(cfg.flipScreen        == false);
    CHECK(cfg.sortMode          == SORT_MODE_API_ORDER);
    CHECK(cfg.favoritesFirst    == true);
    CHECK(cfg.skipClosedRides   == false);
    CHECK(cfg.minWaitMinutes    == 0);
    CHECK_FALSE(mgr.isRideFavorite(1, 10));

    // Values survive (as defaults) across a reload
    mgr.load();
    CHECK_FALSE(mgr.hasEnabledParks());
    CHECK(mgr.getConfig().apiRefreshInterval == DEFAULT_API_REFRESH_INTERVAL);
}
