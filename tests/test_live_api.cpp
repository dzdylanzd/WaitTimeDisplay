// LIVE integration test — requires internet access.
//
// Fetches the full park list from queue-times.com and, for every park,
// verifies that QueueApi::fetchRideData() parses exactly the rides present
// in the raw JSON (both the "lands" grouping and the top-level "rides"
// array used by e.g. Tokyo Disneyland). Catches any park whose response
// format the parser cannot handle.
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

TEST_CASE("LIVE: fetchRideData parses every park on queue-times.com") {
  setvbuf(stdout, nullptr, _IONBF, 0);
  QueueApi api;
  std::vector<int> ids;
  std::vector<String> names, groups;

  REQUIRE_MESSAGE(api.fetchAvailableParks(ids, names, groups),
                  "could not fetch parks.json — is the internet up?");
  REQUIRE(ids.size() > 0);
  printf("Checking %u parks...\n", (unsigned)ids.size());

  int parsed = 0, noData = 0, httpFail = 0;

  for (size_t i = 0; i < ids.size(); i++) {
    const int parkId = ids[i];
    printf("  [%u/%u] park %d '%s'\n", (unsigned)i + 1, (unsigned)ids.size(),
           parkId, names[i].c_str());
    String url = String("https://queue-times.com/parks/") + parkId +
                 "/queue_times.json";

    // Ground truth: count rides in the raw JSON ourselves.
    String payload;
    if (!rawGet(url, payload)) {
      printf("  [skip] park %d '%s': HTTP failed\n", parkId, names[i].c_str());
      httpFail++;
      continue;
    }

    DynamicJsonDocument doc(262144);
    char msg[256];
    snprintf(msg, sizeof(msg), "park %d '%s' (%u bytes)",
             parkId, names[i].c_str(), (unsigned)payload.length());
    INFO(msg);
    REQUIRE_MESSAGE(deserializeJson(doc, payload.c_str()) ==
                    DeserializationError::Ok, "raw JSON did not parse");

    int expected = 0;
    for (JsonObject land : doc["lands"].as<JsonArray>())
      expected += (int)land["rides"].as<JsonArray>().size();
    expected += (int)doc["rides"].as<JsonArray>().size();

    if (expected == 0) {  // park publishes no ride data at all — nothing to parse
      noData++;
      continue;
    }
    if (expected > MAX_RIDES) expected = MAX_RIDES;

    // What the firmware parser produces for the same park.
    RideInfo rides[MAX_RIDES];
    int count = 0;
    bool ok = api.fetchRideData(parkId, rides, count, MAX_RIDES);

    snprintf(msg, sizeof(msg),
             "park %d '%s': raw JSON has %d rides but fetchRideData %s (%d)",
             parkId, names[i].c_str(), expected,
             ok ? "returned" : "FAILED", count);
    INFO(msg);
    CHECK(ok);
    CHECK(count == expected);
    if (ok && count == expected) parsed++;
  }

  printf("Result: %d parks parsed OK, %d with no ride data, %d HTTP failures\n",
         parsed, noData, httpFail);
  // A few transient HTTP failures are tolerated, but not wholesale outage.
  CHECK(httpFail < (int)ids.size() / 4);
}
