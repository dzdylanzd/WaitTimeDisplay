#include "doctest.h"
#include "../src/configmanager.h"
#include "Preferences.h"
#include <ArduinoJson.h>
#include <initializer_list>

// Every test boots like the device does: load() first. That stamps the
// config version (cfg_ver) — without it, the first load() would treat
// freshly saved parks as pre-v2 leftovers and wipe them (see the migration
// tests at the bottom).
static ConfigManager makeBooted() {
    Preferences::resetMock();   // storage is shared across instances now
    ConfigManager mgr;
    mgr.load();
    return mgr;
}

// Dashed 36-char park UUIDs (applyRideSelections requires the real shape).
#define P1 "00000000-0000-0000-0000-000000000001"
#define P2 "00000000-0000-0000-0000-000000000002"
#define P3 "00000000-0000-0000-0000-000000000003"

// Apply ride selections from JSON literals, the way handleSaveConfig does.
static bool applySel(ConfigManager& mgr,
                     const char* filtersJson, const char* favsJson,
                     std::initializer_list<const char*> keepParks,
                     String* outErr = nullptr) {
    DynamicJsonDocument fd(16384), vd(16384);
    if (filtersJson) REQUIRE(deserializeJson(fd, filtersJson) == DeserializationError::Ok);
    if (favsJson)    REQUIRE(deserializeJson(vd, favsJson) == DeserializationError::Ok);
    std::vector<String> keep;
    for (const char* k : keepParks) keep.push_back(String(k));
    String err;
    bool ok = mgr.applyRideSelections(fd.as<JsonObjectConst>(),
                                      vd.as<JsonObjectConst>(), keep, err);
    if (outErr) *outErr = err;
    return ok;
}

// ── parseEnabledParks ─────────────────────────────────────────────────────────

TEST_CASE("parseEnabledParks: valid JSON returns parks") {
    ConfigManager mgr;
    std::vector<String> ids;
    std::vector<String> names;

    bool ok = mgr.parseEnabledParks(
        R"([{"id":"75ea578a-adc8-4116-a54d-dccb60765ef9","name":"Magic Kingdom"},{"id":"47f90d2c-e191-4239-a466-5892ef59a88b","name":"EPCOT"}])",
        ids, names);

    CHECK(ok);
    CHECK(ids.size()   == 2);
    CHECK(names.size() == 2);
    CHECK(ids[0]   == "75ea578a-adc8-4116-a54d-dccb60765ef9");
    CHECK(names[0] == "Magic Kingdom");
    CHECK(ids[1]   == "47f90d2c-e191-4239-a466-5892ef59a88b");
    CHECK(names[1] == "EPCOT");
}

TEST_CASE("parseEnabledParks: empty array returns false") {
    ConfigManager mgr;
    std::vector<String> ids; std::vector<String> names;
    CHECK_FALSE(mgr.parseEnabledParks("[]", ids, names));
    CHECK(ids.empty());
}

TEST_CASE("parseEnabledParks: malformed JSON returns false") {
    ConfigManager mgr;
    std::vector<String> ids; std::vector<String> names;
    CHECK_FALSE(mgr.parseEnabledParks("{not valid json", ids, names));
}

TEST_CASE("parseEnabledParks: entries with missing/empty id are skipped") {
    ConfigManager mgr;
    std::vector<String> ids; std::vector<String> names;
    bool ok = mgr.parseEnabledParks(
        R"([{"id":"","name":"Bad"},{"name":"NoId"},{"id":"good-park-uuid","name":"Good"}])",
        ids, names);
    CHECK(ok);
    CHECK(ids.size() == 1);
    CHECK(ids[0] == "good-park-uuid");
}

TEST_CASE("parseEnabledParks: JSON object instead of array returns false") {
    ConfigManager mgr;
    std::vector<String> ids; std::vector<String> names;
    CHECK_FALSE(mgr.parseEnabledParks(R"({"id":"x","name":"Park"})", ids, names));
    CHECK(ids.empty());
}

// ── isRideEnabled / applyRideSelections ─────────────────────────────────────

TEST_CASE("isRideEnabled: no filter — all rides enabled") {
    ConfigManager mgr = makeBooted();
    CHECK(mgr.isRideEnabled(P1, "ride100"));
    CHECK(mgr.isRideEnabled(P1, "ride999"));
}

TEST_CASE("isRideEnabled: park not in filter — all enabled") {
    ConfigManager mgr = makeBooted();
    REQUIRE(applySel(mgr, R"({")" P1 R"(":["r10","r20","r30"]})", nullptr, {P1, P2}));
    CHECK(mgr.isRideEnabled(P2, "r1"));   // P2 has no filter entry
}

TEST_CASE("isRideEnabled: only listed rides are enabled") {
    ConfigManager mgr = makeBooted();
    REQUIRE(applySel(mgr, R"({")" P1 R"(":["r10","r20","r30"]})", nullptr, {P1}));
    CHECK(mgr.isRideEnabled(P1, "r10"));
    CHECK(mgr.isRideEnabled(P1, "r20"));
    CHECK(mgr.isRideEnabled(P1, "r30"));
    CHECK_FALSE(mgr.isRideEnabled(P1, "r99"));
}

TEST_CASE("applyRideSelections: updating a filter replaces the old one") {
    ConfigManager mgr = makeBooted();
    REQUIRE(applySel(mgr, R"({")" P2 R"(":["r5","r6"]})", nullptr, {P2}));
    CHECK(mgr.isRideEnabled(P2, "r5"));
    CHECK_FALSE(mgr.isRideEnabled(P2, "r9"));

    REQUIRE(applySel(mgr, R"({")" P2 R"(":["r9"]})", nullptr, {P2}));
    CHECK(mgr.isRideEnabled(P2, "r9"));
    CHECK_FALSE(mgr.isRideEnabled(P2, "r5"));
}

TEST_CASE("applyRideSelections: null clears a park's filter") {
    ConfigManager mgr = makeBooted();
    REQUIRE(applySel(mgr, R"({")" P1 R"(":["r1"]})", nullptr, {P1}));
    REQUIRE_FALSE(mgr.isRideEnabled(P1, "r2"));
    REQUIRE(applySel(mgr, R"({")" P1 R"(":null})", nullptr, {P1}));
    CHECK(mgr.isRideEnabled(P1, "r2"));   // back to all-enabled
}

TEST_CASE("applyRideSelections: absent park keeps its stored filter") {
    ConfigManager mgr = makeBooted();
    REQUIRE(applySel(mgr, R"({")" P1 R"(":["r1"]})", nullptr, {P1, P2}));
    // A later save that only touches P2 must not disturb P1.
    REQUIRE(applySel(mgr, R"({")" P2 R"(":["r7"]})", nullptr, {P1, P2}));
    CHECK(mgr.isRideEnabled(P1, "r1"));
    CHECK_FALSE(mgr.isRideEnabled(P1, "r2"));
    CHECK(mgr.isRideEnabled(P2, "r7"));
    CHECK_FALSE(mgr.isRideEnabled(P2, "r8"));
}

TEST_CASE("applyRideSelections: disabling a park drops its stored keys") {
    ConfigManager mgr = makeBooted();
    REQUIRE(applySel(mgr, R"({")" P1 R"(":["r1"]})",
                          R"({")" P1 R"(":["r1"]})", {P1}));
    REQUIRE_FALSE(mgr.isRideEnabled(P1, "r2"));
    REQUIRE(mgr.isRideFavorite(P1, "r1"));
    // Save again with P1 no longer enabled → its keys are cleaned up.
    REQUIRE(applySel(mgr, nullptr, nullptr, {P2}));
    CHECK(mgr.isRideEnabled(P1, "r2"));           // filter gone
    CHECK_FALSE(mgr.isRideFavorite(P1, "r1"));    // favorite gone
}

TEST_CASE("applyRideSelections: empty array behaves like null (no entry)") {
    ConfigManager mgr = makeBooted();
    REQUIRE(applySel(mgr, R"({")" P1 R"(":[]})", nullptr, {P1}));
    CHECK(mgr.isRideEnabled(P1, "anything"));
}

// ── saveEnabledParks round-trip ───────────────────────────────────────────────

TEST_CASE("saveEnabledParks: round-trips via parseEnabledParks") {
    ConfigManager mgr = makeBooted();
    mgr.saveEnabledParks(R"([{"id":"park3","name":"Disneyland"},{"id":"park8","name":"Hollywood Studios"}])");

    const RuntimeConfig& cfg = mgr.getConfig();
    REQUIRE(cfg.enabledParkIds.size() == 2);
    CHECK(cfg.enabledParkIds[0] == "park3");
    CHECK(cfg.enabledParkNames[0] == "Disneyland");
    CHECK(cfg.enabledParkIds[1] == "park8");
}

TEST_CASE("hasEnabledParks: false when empty, true after setting") {
    ConfigManager mgr = makeBooted();
    CHECK_FALSE(mgr.hasEnabledParks());
    mgr.saveEnabledParks(R"([{"id":"park1","name":"Park"}])");
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
    mgr.saveDisplaySettings(40, true, 21 * 60, 6 * 60 + 30, 10, false, true,
                            "Europe/Amsterdam");
    const auto& cfg = mgr.getConfig();
    CHECK(cfg.brightness        == 40);
    CHECK(cfg.quietHoursEnabled == true);
    CHECK(cfg.quietStartMin     == 21 * 60);
    CHECK(cfg.quietEndMin       == 6 * 60 + 30);
    CHECK(cfg.quietBrightness   == 10);
    CHECK(cfg.ledEnabled        == false);
    CHECK(cfg.flipScreen        == true);
    CHECK(cfg.deviceTimezone    == "Europe/Amsterdam");
}

// ── savePalette ───────────────────────────────────────────────────────────────

TEST_CASE("savePalette: value reflected in getConfig, out-of-range falls to 0") {
    ConfigManager mgr;
    CHECK(mgr.getConfig().colorPalette == 0);   // default
    mgr.savePalette(3);
    CHECK(mgr.getConfig().colorPalette == 3);
    mgr.savePalette(COLOR_PALETTE_COUNT);       // one past the end → default
    CHECK(mgr.getConfig().colorPalette == 0);
}

// ── saveWaitConfig ────────────────────────────────────────────────────────────

TEST_CASE("saveWaitConfig: thresholds and colours reflected in getConfig") {
    ConfigManager mgr;
    // Defaults first
    CHECK(mgr.getConfig().waitTh1 == 15);
    CHECK(mgr.getConfig().waitTh2 == 30);
    CHECK(mgr.getConfig().waitTh3 == 45);
    CHECK(mgr.getConfig().waitColors[0] == 0x00E676);
    CHECK(mgr.getConfig().waitColors[4] == 0x18FFFF);

    const uint32_t cols[5] = { 0x112233, 0x445566, 0x778899, 0xAABBCC, 0xDDEEFF };
    mgr.saveWaitConfig(5, 10, 20, cols);
    const auto& cfg = mgr.getConfig();
    CHECK(cfg.waitTh1 == 5);
    CHECK(cfg.waitTh2 == 10);
    CHECK(cfg.waitTh3 == 20);
    for (int i = 0; i < 5; i++) CHECK(cfg.waitColors[i] == cols[i]);
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
    ConfigManager mgr = makeBooted();
    CHECK_FALSE(mgr.isRideFavorite(P1, "r100"));
}

TEST_CASE("isRideFavorite: only listed rides are favorites") {
    ConfigManager mgr = makeBooted();
    REQUIRE(applySel(mgr, nullptr, R"({")" P1 R"(":["r10","r20"]})", {P1}));
    CHECK(mgr.isRideFavorite(P1, "r10"));
    CHECK(mgr.isRideFavorite(P1, "r20"));
    CHECK_FALSE(mgr.isRideFavorite(P1, "r30"));
    // Unlike the ride filter, a missing park means NOT favorite
    CHECK_FALSE(mgr.isRideFavorite(P2, "r10"));
}

TEST_CASE("isRideFavorite: updated favorites replace the old set") {
    ConfigManager mgr = makeBooted();
    REQUIRE(applySel(mgr, nullptr, R"({")" P2 R"(":["r7"]})", {P2}));
    CHECK(mgr.isRideFavorite(P2, "r7"));
    REQUIRE(applySel(mgr, nullptr, R"({")" P2 R"(":["r8"]})", {P2}));
    CHECK(mgr.isRideFavorite(P2, "r8"));
    CHECK_FALSE(mgr.isRideFavorite(P2, "r7"));
}

// ── export / import round-trip ────────────────────────────────────────────────

TEST_CASE("exportRideSelections: hashes round-trip through an import") {
    ConfigManager mgr = makeBooted();
    REQUIRE(applySel(mgr,
        R"({")" P1 R"(":["aaaaaaaa111122223333444444440001","aaaaaaaa111122223333444444440002"]})",
        R"({")" P1 R"(":["aaaaaaaa111122223333444444440002"]})", {P1}));

    DynamicJsonDocument out(8192);
    JsonObject rf  = out.createNestedObject("rideFilters");
    JsonObject fav = out.createNestedObject("rideFavorites");
    mgr.exportRideSelections(rf, fav);

    REQUIRE(rf[P1].as<JsonArray>().size() == 2);
    String h0 = rf[P1][0].as<const char*>();
    CHECK(h0.length() == 8);                 // exported as 8-hex hashes
    REQUIRE(fav[P1].as<JsonArray>().size() == 1);

    // Re-import the exported hashes into a fresh manager (new device).
    ConfigManager mgr2 = makeBooted();
    String json; serializeJson(out, json);
    DynamicJsonDocument in(8192);
    REQUIRE(deserializeJson(in, json) == DeserializationError::Ok);
    std::vector<String> keep = { String(P1) };
    String err;
    REQUIRE(mgr2.applyRideSelections(in["rideFilters"].as<JsonObjectConst>(),
                                     in["rideFavorites"].as<JsonObjectConst>(),
                                     keep, err));
    CHECK(mgr2.isRideEnabled(P1, "aaaaaaaa111122223333444444440001"));
    CHECK(mgr2.isRideEnabled(P1, "aaaaaaaa111122223333444444440002"));
    CHECK_FALSE(mgr2.isRideEnabled(P1, "aaaaaaaa111122223333444444440003"));
    CHECK(mgr2.isRideFavorite(P1, "aaaaaaaa111122223333444444440002"));
    CHECK_FALSE(mgr2.isRideFavorite(P1, "aaaaaaaa111122223333444444440001"));
}

// ── capacity: many fully-filtered parks ───────────────────────────────────────

TEST_CASE("applyRideSelections: 12 parks with 60-ride filters all fit") {
    ConfigManager mgr = makeBooted();
    std::vector<String> keep;
    for (int p = 0; p < 12; p++) {
        char parkId[37];
        snprintf(parkId, sizeof(parkId),
                 "00000000-0000-0000-0000-0000000000%02d", p);
        keep.push_back(String(parkId));
        // build {"<parkId>":[60 ids]} directly
        DynamicJsonDocument fd(16384);
        JsonObject root = fd.to<JsonObject>();
        JsonArray ids = root.createNestedArray(parkId);
        for (int r = 0; r < 60; r++) {
            char rid[33];
            snprintf(rid, sizeof(rid), "%08x11112222333344444444%04x", p, r);
            ids.add(rid);
        }
        String err;
        REQUIRE(mgr.applyRideSelections(fd.as<JsonObjectConst>(),
                                        JsonObjectConst(), keep, err));
    }
    // Spot-check first and last park
    CHECK(mgr.isRideEnabled("00000000-0000-0000-0000-000000000000",
                            "0000000011112222333344444444" "0000"));
    CHECK_FALSE(mgr.isRideEnabled("00000000-0000-0000-0000-000000000000",
                                  "ffffffff111122223333444444440000"));
    CHECK(mgr.isRideEnabled("00000000-0000-0000-0000-000000000011",
                            "0000000b11112222333344444444" "003b"));
}

TEST_CASE("applyRideSelections: NVS-full failure after a prior park's success is reported, not silently lost") {
    ConfigManager mgr = makeBooted();
    // Park 1 succeeds first (kv iteration order in ArduinoJson follows
    // insertion order), then park 2's write is forced to fail like a full
    // NVS partition would.
    REQUIRE(applySel(mgr,
        R"({")" P1 R"(":["aaaaaaaa111122223333444444440001"]})", nullptr, {P1, P2}));
    CHECK(mgr.isRideEnabled(P1, "aaaaaaaa111122223333444444440001"));

    DynamicJsonDocument fd(16384);
    JsonObject root = fd.to<JsonObject>();
    root.createNestedArray(P1).add("aaaaaaaa111122223333444444440009");
    root.createNestedArray(P2).add("bbbbbbbb111122223333444444440001");
    std::vector<String> keep = { String(P1), String(P2) };
    String err;
    Preferences::failPutStringOnCall() = 2;   // 1st write succeeds, 2nd fails
    bool ok = mgr.applyRideSelections(fd.as<JsonObjectConst>(),
                                      JsonObjectConst(), keep, err);
    Preferences::failPutStringOnCall() = 0;
    CHECK_FALSE(ok);
    CHECK(err.indexOf("already saved") >= 0);
    // P1 (inserted first, so written first) kept its new selection from this
    // same call even though the overall call reported failure.
    CHECK(mgr.isRideEnabled(P1, "aaaaaaaa111122223333444444440009"));
    CHECK_FALSE(mgr.isRideEnabled(P1, "aaaaaaaa111122223333444444440001"));
}

// ── load() persistence round-trip ─────────────────────────────────────────────

TEST_CASE("load: saved settings survive a reload from Preferences") {
    ConfigManager mgr = makeBooted();
    mgr.saveTimings(60000, 5000, 15000, 30000);
    mgr.saveDisplaySettings(55, true, 20 * 60, 8 * 60, 25, false, true,
                            "Europe/Amsterdam");
    mgr.saveRideOptions(SORT_MODE_WAIT_DESC, false, true, 10);
    mgr.savePalette(2);
    const uint32_t waitCols[5] = { 0x111111, 0x222222, 0x333333, 0x444444, 0x555555 };
    mgr.saveWaitConfig(8, 16, 32, waitCols);
    mgr.saveEnabledParks(R"([{"id":")" P1 R"(","name":"Paris"}])");
    REQUIRE(applySel(mgr, R"({")" P1 R"(":["r1","r2"]})",
                          R"({")" P1 R"(":["r2"]})", {P1}));

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
    CHECK(cfg.deviceTimezone     == "Europe/Amsterdam");
    CHECK(cfg.colorPalette       == 2);
    CHECK(cfg.waitTh1            == 8);
    CHECK(cfg.waitTh2            == 16);
    CHECK(cfg.waitTh3            == 32);
    CHECK(cfg.waitColors[0]      == 0x111111);
    CHECK(cfg.waitColors[4]      == 0x555555);
    CHECK(cfg.sortMode           == SORT_MODE_WAIT_DESC);
    CHECK(cfg.favoritesFirst     == false);
    CHECK(cfg.skipClosedRides    == true);
    CHECK(cfg.minWaitMinutes     == 10);
    REQUIRE(cfg.enabledParkIds.size() == 1);
    CHECK(cfg.enabledParkIds[0]  == P1);
    CHECK(mgr.isRideEnabled(P1, "r1"));
    CHECK_FALSE(mgr.isRideEnabled(P1, "r3"));
    CHECK(mgr.isRideFavorite(P1, "r2"));
    CHECK_FALSE(mgr.isRideFavorite(P1, "r1"));
}

// ── config-version migration ──────────────────────────────────────────────────

// (The Preferences mock is per-process/shared-map, so seeding raw keys via a
//  separate Preferences handle is exactly what an old firmware would have
//  left behind; load() is what runs the migration, as on a real boot.)
TEST_CASE("migration: pre-v2 park/ride keys are wiped, other settings survive") {
    Preferences::resetMock();
    ConfigManager mgr;
    // Old firmware leftovers: numeric ids, no cfg_ver key.
    {
        Preferences p;
        p.begin(NVS_NAMESPACE, false);
        p.putString("enabled_pks", R"([{"id":1,"name":"Magic Kingdom"}])");
        p.putString("ride_flt", R"({"1":[10,20]})");
        p.putString("ride_fav", R"({"1":[10]})");
        p.end();
    }
    mgr.saveDisplaySettings(55, true, 20 * 60, 8 * 60, 25, false, true,
                            "Europe/Amsterdam");
    mgr.saveTimings(60000, 5000, 15000, 30000);

    mgr.load();   // first boot on the new firmware → migration runs

    const auto& cfg = mgr.getConfig();
    CHECK_FALSE(mgr.hasEnabledParks());              // parks wiped
    CHECK(cfg.brightness      == 55);                // display settings survive
    CHECK(cfg.deviceTimezone  == "Europe/Amsterdam");
    CHECK(cfg.apiRefreshInterval == 60000);          // timings survive
}

TEST_CASE("migration: v2 → v3 keeps parks, drops the old filter blobs") {
    Preferences::resetMock();
    ConfigManager mgr;
    {
        Preferences p;
        p.begin(NVS_NAMESPACE, false);
        p.putInt("cfg_ver", 2);                      // a v2 device
        p.putString("enabled_pks", R"([{"id":")" P1 R"(","name":"Paris"}])");
        p.putString("ride_flt", R"({")" P1 R"(":["aaaa"]})");
        p.putString("ride_fav", R"({")" P1 R"(":["aaaa"]})");
        p.end();
    }
    mgr.load();
    CHECK(mgr.hasEnabledParks());                    // parks SURVIVE v2→v3
    CHECK(mgr.getConfig().enabledParkIds[0] == P1);
    CHECK(mgr.isRideEnabled(P1, "anything"));        // old blob dropped
    CHECK_FALSE(mgr.isRideFavorite(P1, "aaaa"));
}

TEST_CASE("migration: runs only once — v3 config is left alone") {
    Preferences::resetMock();
    ConfigManager mgr;
    mgr.load();   // stamps cfg_ver = 3
    mgr.saveEnabledParks(R"([{"id":")" P1 R"(","name":"Park"}])");
    REQUIRE(applySel(mgr, R"({")" P1 R"(":["r1"]})", nullptr, {P1}));

    mgr.load();   // subsequent boot — must NOT wipe the config
    CHECK(mgr.hasEnabledParks());
    CHECK(mgr.isRideEnabled(P1, "r1"));
    CHECK_FALSE(mgr.isRideEnabled(P1, "r2"));
}

// ── factoryReset ──────────────────────────────────────────────────────────────

TEST_CASE("factoryReset: wipes parks/filters/timings back to defaults") {
    ConfigManager mgr = makeBooted();
    mgr.saveTimings(120000, 15000, 25000, 45000);
    mgr.saveEnabledParks(R"([{"id":")" P1 R"(","name":"Park"}])");
    REQUIRE(applySel(mgr, R"({")" P1 R"(":["r10","r20"]})",
                          R"({")" P1 R"(":["r10"]})", {P1}));
    mgr.saveDisplaySettings(40, true, 21 * 60, 6 * 60, 10, false, true,
                            "Europe/Amsterdam");
    mgr.saveRideOptions(SORT_MODE_WAIT_DESC, false, true, 15);
    mgr.savePalette(4);
    const uint32_t waitCols[5] = { 1, 2, 3, 4, 5 };
    mgr.saveWaitConfig(5, 10, 20, waitCols);
    REQUIRE(mgr.hasEnabledParks());
    REQUIRE_FALSE(mgr.isRideEnabled(P1, "r99"));
    REQUIRE(mgr.isRideFavorite(P1, "r10"));

    mgr.factoryReset();

    const auto& cfg = mgr.getConfig();
    CHECK_FALSE(mgr.hasEnabledParks());
    CHECK(cfg.apiRefreshInterval    == DEFAULT_API_REFRESH_INTERVAL);
    CHECK(cfg.rotateInterval        == DEFAULT_ROTATE_INTERVAL);
    CHECK(cfg.closedParkDisplayTime == DEFAULT_CLOSED_PARK_DISPLAY_TIME);
    CHECK(cfg.timeUpdateInterval    == DEFAULT_TIME_UPDATE_INTERVAL);
    CHECK(mgr.isRideEnabled(P1, "r99"));  // filter gone — everything enabled
    CHECK(cfg.brightness        == 100);
    CHECK(cfg.quietHoursEnabled == false);
    CHECK(cfg.ledEnabled        == true);
    CHECK(cfg.flipScreen        == false);
    CHECK(cfg.deviceTimezone    == "");
    CHECK(cfg.colorPalette      == 0);
    CHECK(cfg.waitTh1           == 15);
    CHECK(cfg.waitTh2           == 30);
    CHECK(cfg.waitTh3           == 45);
    CHECK(cfg.waitColors[0]     == 0x00E676);
    CHECK(cfg.waitColors[4]     == 0x18FFFF);
    CHECK(cfg.sortMode          == SORT_MODE_API_ORDER);
    CHECK(cfg.favoritesFirst    == true);
    CHECK(cfg.skipClosedRides   == false);
    CHECK(cfg.minWaitMinutes    == 0);
    CHECK_FALSE(mgr.isRideFavorite(P1, "r10"));

    // Values survive (as defaults) across a reload
    mgr.load();
    CHECK_FALSE(mgr.hasEnabledParks());
    CHECK(mgr.getConfig().apiRefreshInterval == DEFAULT_API_REFRESH_INTERVAL);
}
