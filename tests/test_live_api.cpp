// LIVE integration test — requires internet access.
//
// Fetches the full park list from api.themeparks.wiki and, for every park,
// verifies that QueueApi::fetchRideData() parses exactly the ATTRACTION and
// SHOW entities present in the raw /live JSON. Catches any park whose
// response format the parser cannot handle.
//
// Built as a separate executable (queuewatch_live_tests) so the normal
// offline unit tests stay fast and deterministic.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
// NOTE: explicit live/ path — a bare "HTTPClient.h" would resolve to the
// mock in this file's own directory and clash with the WinHTTP client that
// queueapi.cpp is compiled against.
#include "live/HTTPClient.h"   // → sim WinHTTP (real HTTPS)
#include "../src/queueapi.h"
#include "../src/config.h"
#include <ArduinoJson.h>
#include <cstdio>

static bool rawGet(const String& url, String& out) {
  WiFiClientSecure client;
  HTTPClient http;
  if (!http.begin(client, url)) return false;
  http.setTimeout(10000);
  int code = http.GET();
  if (code != 200) { http.end(); return false; }
  out = http.getString();
  http.end();
  return true;
}

TEST_CASE("LIVE: fetchRideData parses every park on api.themeparks.wiki") {
  setvbuf(stdout, nullptr, _IONBF, 0);
  QueueApi api;
  std::vector<String> ids, names, groups;

  REQUIRE_MESSAGE(api.fetchAvailableParks(ids, names, groups),
                  "could not fetch /destinations — is the internet up?");
  REQUIRE(ids.size() > 0);
  printf("Checking %u parks...\n", (unsigned)ids.size());

  int parsed = 0, noData = 0, httpFail = 0;

  for (size_t i = 0; i < ids.size(); i++) {
    const String& parkId = ids[i];
    printf("  [%u/%u] park %s '%s'\n", (unsigned)i + 1, (unsigned)ids.size(),
           parkId.c_str(), names[i].c_str());
    String url = String("https://api.themeparks.wiki/v1/entity/") + parkId +
                 "/live";

    // Ground truth: count displayable entities in the raw JSON ourselves.
    String payload;
    if (!rawGet(url, payload)) {
      printf("  [skip] park %s '%s': HTTP failed\n",
             parkId.c_str(), names[i].c_str());
      httpFail++;
      continue;
    }

    DynamicJsonDocument doc(1048576);
    char msg[256];
    snprintf(msg, sizeof(msg), "park %s '%s' (%u bytes)",
             parkId.c_str(), names[i].c_str(), (unsigned)payload.length());
    INFO(msg);
    REQUIRE_MESSAGE(deserializeJson(doc, payload.c_str()) ==
                    DeserializationError::Ok, "raw JSON did not parse");

    // Mirror the parser's inclusion rule: ATTRACTION or SHOW with a
    // non-empty id (PARK, RESTAURANT etc. are skipped).
    int expected = 0;
    for (JsonObject e : doc["liveData"].as<JsonArray>()) {
      const char* type = e["entityType"] | "";
      if (strcmp(type, "ATTRACTION") != 0 && strcmp(type, "SHOW") != 0)
        continue;
      if (strlen(e["id"] | "") == 0) continue;
      expected++;
    }

    if (expected == 0) {  // park publishes no live entities — nothing to parse
      noData++;
      continue;
    }
    if (expected > MAX_RIDES) expected = MAX_RIDES;

    // What the firmware parser produces for the same park.
    RideInfo rides[MAX_RIDES];
    int count = 0;
    bool ok = api.fetchRideData(parkId, "2026-01-01", 0,
                                rides, count, MAX_RIDES);

    snprintf(msg, sizeof(msg),
             "park %s '%s': raw JSON has %d entities but fetchRideData %s (%d)",
             parkId.c_str(), names[i].c_str(), expected,
             ok ? "returned" : "FAILED", count);
    INFO(msg);
    CHECK(ok);
    CHECK(count == expected);
    if (ok && count == expected) parsed++;
  }

  printf("Result: %d parks parsed OK, %d with no live data, %d HTTP failures\n",
         parsed, noData, httpFail);
  // A few transient HTTP failures are tolerated, but not wholesale outage.
  CHECK(httpFail < (int)ids.size() / 4);
}
