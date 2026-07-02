#include "doctest.h"
#include "HTTPClient.h"   // mock — from tests/ (first on include path)
#include "../src/queueapi.h"
#include "../src/config.h"

// Sample JSON matching the real queue-times.com format
static const char* RIDE_JSON = R"({
  "lands": [
    {
      "id": 1,
      "name": "Fantasyland",
      "rides": [
        {"id": 101, "name": "Magic Carpets", "wait_time": 10, "is_open": true},
        {"id": 102, "name": "Dumbo",          "wait_time": 25, "is_open": true},
        {"id": 103, "name": "Closed Ride",    "wait_time": 0,  "is_open": false}
      ]
    },
    {
      "id": 2,
      "name": "Tomorrowland",
      "rides": [
        {"id": 201, "name": "Space Mountain", "wait_time": 45, "is_open": true}
      ]
    }
  ]
})";

static const char* PARKS_JSON = R"([
  {
    "name": "Disney World",
    "parks": [
      {"id": 1, "name": "Magic Kingdom",      "timezone": "America/New_York"},
      {"id": 7, "name": "EPCOT",              "timezone": "America/New_York"}
    ]
  },
  {
    "name": "Disneyland Resort",
    "parks": [
      {"id": 16, "name": "Disneyland Park",   "timezone": "America/Los_Angeles"}
    ]
  }
])";

// Tokyo Disneyland/DisneySea format: empty "lands", rides at top level
static const char* TOKYO_RIDE_JSON = R"({
  "lands": [],
  "rides": [
    {"id": 301, "name": "Journey to the Center of the Earth", "wait_time": 60, "is_open": true},
    {"id": 302, "name": "Toy Story Mania!",                   "wait_time": 90, "is_open": true},
    {"id": 303, "name": "Aquatopia",                          "wait_time": 0,  "is_open": false}
  ]
})";

// ── fetchRideData ─────────────────────────────────────────────────────────────

TEST_CASE("fetchRideData: parses rides correctly") {
    MockHTTP::clear();
    MockHTTP::set("https://queue-times.com/parks/1/queue_times.json", RIDE_JSON);

    QueueApi api;
    RideInfo rides[MAX_RIDES];
    int count = 0;
    bool ok = api.fetchRideData(1, rides, count, MAX_RIDES);

    CHECK(ok);
    REQUIRE(count == 4);

    CHECK(rides[0].id       == 101);
    CHECK(rides[0].name     == "Magic Carpets");
    CHECK(rides[0].waitTime == 10);
    CHECK(rides[0].isOpen   == true);

    CHECK(rides[1].id       == 102);
    CHECK(rides[1].waitTime == 25);

    CHECK(rides[2].id       == 103);
    CHECK(rides[2].isOpen   == false);

    CHECK(rides[3].id       == 201);
    CHECK(rides[3].name     == "Space Mountain");
    CHECK(rides[3].waitTime == 45);
}

TEST_CASE("fetchRideData: rides carry their land name") {
    MockHTTP::clear();
    MockHTTP::set("https://queue-times.com/parks/1/queue_times.json", RIDE_JSON);

    QueueApi api;
    RideInfo rides[MAX_RIDES]; int count = 0;
    REQUIRE(api.fetchRideData(1, rides, count, MAX_RIDES));
    REQUIRE(count == 4);
    CHECK(rides[0].land == "Fantasyland");
    CHECK(rides[2].land == "Fantasyland");
    CHECK(rides[3].land == "Tomorrowland");
    // Fetch annotations start neutral
    CHECK(rides[0].trend == 0);
    CHECK(rides[0].favorite == false);
}

TEST_CASE("fetchRideData: parses top-level rides array (Tokyo format)") {
    MockHTTP::clear();
    MockHTTP::set("https://queue-times.com/parks/274/queue_times.json", TOKYO_RIDE_JSON);

    QueueApi api;
    RideInfo rides[MAX_RIDES];
    int count = 0;
    bool ok = api.fetchRideData(274, rides, count, MAX_RIDES);

    CHECK(ok);
    REQUIRE(count == 3);
    CHECK(rides[0].id       == 301);
    CHECK(rides[0].name     == "Journey to the Center of the Earth");
    CHECK(rides[0].waitTime == 60);
    CHECK(rides[1].isOpen   == true);
    CHECK(rides[2].isOpen   == false);
    CHECK(rides[0].land     == "");   // no lands → no land name
}

TEST_CASE("fetchRideData: returns false on HTTP error") {
    MockHTTP::clear();
    MockHTTP::set("https://queue-times.com/parks/1/queue_times.json", "", 500);

    QueueApi api;
    RideInfo rides[MAX_RIDES]; int count = 0;
    CHECK_FALSE(api.fetchRideData(1, rides, count, MAX_RIDES));
}

TEST_CASE("fetchRideData: returns false for parkId <= 0") {
    QueueApi api;
    RideInfo rides[MAX_RIDES]; int count = 0;
    CHECK_FALSE(api.fetchRideData(0, rides, count, MAX_RIDES));
    CHECK_FALSE(api.fetchRideData(-1, rides, count, MAX_RIDES));
}

TEST_CASE("fetchRideData: respects maxRides limit") {
    MockHTTP::clear();
    MockHTTP::set("https://queue-times.com/parks/1/queue_times.json", RIDE_JSON);

    QueueApi api;
    RideInfo rides[2]; int count = 0;
    bool ok = api.fetchRideData(1, rides, count, 2);
    CHECK(ok);
    CHECK(count == 2);  // capped at maxRides
}

TEST_CASE("fetchRideData: malformed JSON returns false") {
    MockHTTP::clear();
    MockHTTP::set("https://queue-times.com/parks/1/queue_times.json", "{bad json");

    QueueApi api;
    RideInfo rides[MAX_RIDES]; int count = 0;
    CHECK_FALSE(api.fetchRideData(1, rides, count, MAX_RIDES));
}

// ── fetchAvailableParks ───────────────────────────────────────────────────────

TEST_CASE("fetchAvailableParks: parses all parks and groups") {
    MockHTTP::clear();
    MockHTTP::set("https://queue-times.com/parks.json", PARKS_JSON);

    QueueApi api;
    std::vector<int>    ids;
    std::vector<String> names, groups;
    bool ok = api.fetchAvailableParks(ids, names, groups);

    CHECK(ok);
    REQUIRE(ids.size() == 3);
    CHECK(ids[0]    == 1);
    CHECK(names[0]  == "Magic Kingdom");
    CHECK(groups[0] == "Disney World");
    CHECK(ids[2]    == 16);
    CHECK(names[2]  == "Disneyland Park");
    CHECK(groups[2] == "Disneyland Resort");
}

TEST_CASE("fetchAvailableParks: returns false on HTTP error") {
    MockHTTP::clear();
    MockHTTP::set("https://queue-times.com/parks.json", "", 503);

    QueueApi api;
    std::vector<int> ids; std::vector<String> names, groups;
    CHECK_FALSE(api.fetchAvailableParks(ids, names, groups));
}

// ── getParkTimezone ───────────────────────────────────────────────────────────

TEST_CASE("getParkTimezone: returns correct timezone from parks.json") {
    MockHTTP::clear();
    MockHTTP::set("https://queue-times.com/parks.json", PARKS_JSON);

    QueueApi api;
    CHECK(api.getParkTimezone(1)  == "America/New_York");
    CHECK(api.getParkTimezone(16) == "America/Los_Angeles");
}

TEST_CASE("getParkTimezone: unknown park returns UTC") {
    MockHTTP::clear();
    MockHTTP::set("https://queue-times.com/parks.json", PARKS_JSON);

    QueueApi api;
    CHECK(api.getParkTimezone(999) == "UTC");
}

TEST_CASE("getParkTimezone: caches result (second call uses cache, not HTTP)") {
    MockHTTP::clear();
    MockHTTP::set("https://queue-times.com/parks.json", PARKS_JSON);

    QueueApi api;
    String tz1 = api.getParkTimezone(7);  // fetches HTTP
    MockHTTP::clear();                     // remove mock — cache hit must not re-fetch
    String tz2 = api.getParkTimezone(7);  // should hit cache

    CHECK(tz1 == "America/New_York");
    CHECK(tz2 == "America/New_York");
}
