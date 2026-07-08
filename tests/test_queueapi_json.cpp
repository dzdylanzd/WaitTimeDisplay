#include "doctest.h"
#include "HTTPClient.h"   // mock — from tests/ (first on include path)
#include "../src/queueapi.h"
#include "../src/config.h"

// Park id used across the fixtures (dashed form — appears in URLs).
#define PARK_ID  "75ea578a-adc8-4116-a54d-dccb60765ef9"
#define LIVE_URL "https://api.themeparks.wiki/v1/entity/" PARK_ID "/live"

static const char* TODAY = "2026-07-07";

// Sample JSON matching the real themeparks.wiki /entity/{id}/live format.
// Exercises: all four statuses, a SHOW with three showtimes (one tomorrow),
// a RESTAURANT and the PARK entity (both skipped), and a paid-queue-only
// attraction (no STANDBY → waitTime stays -1).
static const char* LIVE_JSON = R"({
  "id": ")" PARK_ID R"(",
  "liveData": [
    {"id": ")" PARK_ID R"(", "name": "Magic Kingdom Park", "entityType": "PARK",
     "status": "OPERATING"},
    {"id": "AAAAAAAA-1111-2222-3333-444444444444", "name": "Space Mountain",
     "entityType": "ATTRACTION", "status": "OPERATING",
     "queue": {"STANDBY": {"waitTime": 45}}},
    {"id": "bbbbbbbb-1111-2222-3333-444444444444", "name": "Broken Coaster",
     "entityType": "ATTRACTION", "status": "DOWN",
     "queue": {"STANDBY": {"waitTime": 0}}},
    {"id": "cccccccc-1111-2222-3333-444444444444", "name": "Sleepy Ride",
     "entityType": "ATTRACTION", "status": "CLOSED",
     "queue": {"STANDBY": {"waitTime": null}}},
    {"id": "dddddddd-1111-2222-3333-444444444444", "name": "Old Flume",
     "entityType": "ATTRACTION", "status": "REFURBISHMENT"},
    {"id": "eeeeeeee-1111-2222-3333-444444444444", "name": "Festival Parade",
     "entityType": "SHOW", "status": "OPERATING",
     "showtimes": [
       {"type": "Performance Time", "startTime": "2026-07-07T10:00:00-04:00", "endTime": "2026-07-07T10:20:00-04:00"},
       {"type": "Performance Time", "startTime": "2026-07-07T15:00:00-04:00", "endTime": "2026-07-07T15:20:00-04:00"},
       {"type": "Performance Time", "startTime": "2026-07-08T09:00:00-04:00", "endTime": "2026-07-08T09:20:00-04:00"}
     ]},
    {"id": "ffffffff-1111-2222-3333-444444444444", "name": "Burger Barn",
     "entityType": "RESTAURANT", "status": "OPERATING"},
    {"id": "99999999-1111-2222-3333-444444444444", "name": "Premium Ride",
     "entityType": "ATTRACTION", "status": "OPERATING",
     "queue": {"PAID_RETURN_TIME": {"state": "AVAILABLE",
               "price": {"amount": 1200, "currency": "USD"}}}}
  ]
})";

static const char* DESTINATIONS_JSON = R"({
  "destinations": [
    {"id": "e957da41-3552-4cf6-b636-5babc5cbc4e5", "name": "Walt Disney World Resort",
     "parks": [
       {"id": ")" PARK_ID R"(", "name": "Magic Kingdom Park"},
       {"id": "47f90d2c-e191-4239-a466-5892ef59a88b", "name": "EPCOT"}
     ]},
    {"id": "faff60df-c766-4470-8adb-dee78e813f42", "name": "Efteling",
     "parks": [
       {"id": "0b23d5b7-1111-2222-3333-444444444444", "name": "Efteling Themepark"}
     ]}
  ]
})";

// ── fetchRideData ─────────────────────────────────────────────────────────────

TEST_CASE("fetchRideData: parses attractions with all four statuses") {
    MockHTTP::clear();
    MockHTTP::set(LIVE_URL, LIVE_JSON);

    QueueApi api;
    RideInfo rides[MAX_RIDES]; int count = 0;
    REQUIRE(api.fetchRideData(PARK_ID, TODAY, 0, rides, count, MAX_RIDES));
    REQUIRE(count == 6);   // 5 attractions + 1 show; PARK + RESTAURANT skipped

    CHECK(rides[0].id.length() == 32);
    CHECK(rides[0].id       == "aaaaaaaa111122223333444444444444");
    CHECK(rides[0].name     == "Space Mountain");
    CHECK(rides[0].status   == RideStatus::Operating);
    CHECK(rides[0].isOpen() == true);
    CHECK(rides[0].waitTime == 45);
    CHECK(rides[0].kind     == EntityKind::Attraction);

    CHECK(rides[1].status   == RideStatus::Down);
    CHECK(rides[1].isOpen() == false);
    CHECK(rides[2].status   == RideStatus::Closed);
    CHECK(rides[2].waitTime == -1);   // waitTime null
    CHECK(rides[3].status   == RideStatus::Refurbishment);

    // Annotations start neutral
    CHECK(rides[0].trend == 0);
    CHECK(rides[0].favorite == false);
}

TEST_CASE("fetchRideData: ride ids are normalized (undashed lowercase)") {
    MockHTTP::clear();
    MockHTTP::set(LIVE_URL, LIVE_JSON);

    QueueApi api;
    RideInfo rides[MAX_RIDES]; int count = 0;
    REQUIRE(api.fetchRideData(PARK_ID, TODAY, 0, rides, count, MAX_RIDES));
    // Fixture id is uppercase + dashed: AAAAAAAA-1111-...
    CHECK(rides[0].id == "aaaaaaaa111122223333444444444444");
}

TEST_CASE("fetchRideData: show picks the next showtime after nowMin") {
    MockHTTP::clear();
    MockHTTP::set(LIVE_URL, LIVE_JSON);
    QueueApi api;
    RideInfo rides[MAX_RIDES]; int count = 0;

    // Before the first show (10:00 → 600) — earliest wins.
    REQUIRE(api.fetchRideData(PARK_ID, TODAY, 0, rides, count, MAX_RIDES));
    REQUIRE(rides[4].kind == EntityKind::Show);
    CHECK(rides[4].nextShowMin == 600);
    CHECK(rides[4].waitTime == -1);       // shows have no wait

    // Between shows — the 15:00 (900) one is next. Exactly at a showtime
    // still counts (>=).
    REQUIRE(api.fetchRideData(PARK_ID, TODAY, 601, rides, count, MAX_RIDES));
    CHECK(rides[4].nextShowMin == 900);
    REQUIRE(api.fetchRideData(PARK_ID, TODAY, 900, rides, count, MAX_RIDES));
    CHECK(rides[4].nextShowMin == 900);

    // After the last show today — tomorrow's 09:00 must NOT be picked.
    REQUIRE(api.fetchRideData(PARK_ID, TODAY, 901, rides, count, MAX_RIDES));
    CHECK(rides[4].nextShowMin == -1);
}

TEST_CASE("fetchRideData: unknown todayDate matches no showtimes") {
    MockHTTP::clear();
    MockHTTP::set(LIVE_URL, LIVE_JSON);
    QueueApi api;
    RideInfo rides[MAX_RIDES]; int count = 0;
    // Empty date (pre-NTP) — shows simply carry no next showtime.
    REQUIRE(api.fetchRideData(PARK_ID, "", 0, rides, count, MAX_RIDES));
    CHECK(rides[4].nextShowMin == -1);
}

TEST_CASE("fetchRideData: PARK and RESTAURANT entities are skipped, park status captured") {
    MockHTTP::clear();
    MockHTTP::set(LIVE_URL, LIVE_JSON);
    QueueApi api;
    RideInfo rides[MAX_RIDES]; int count = 0;
    RideStatus parkStatus = RideStatus::Closed;
    REQUIRE(api.fetchRideData(PARK_ID, TODAY, 0, rides, count, MAX_RIDES, &parkStatus));
    CHECK(parkStatus == RideStatus::Operating);
    for (int i = 0; i < count; i++) {
        CHECK(rides[i].name != "Magic Kingdom Park");
        CHECK(rides[i].name != "Burger Barn");
    }
}

TEST_CASE("fetchRideData: paid-queue-only attraction has no standby wait") {
    MockHTTP::clear();
    MockHTTP::set(LIVE_URL, LIVE_JSON);
    QueueApi api;
    RideInfo rides[MAX_RIDES]; int count = 0;
    REQUIRE(api.fetchRideData(PARK_ID, TODAY, 0, rides, count, MAX_RIDES));
    CHECK(rides[5].name     == "Premium Ride");
    CHECK(rides[5].status   == RideStatus::Operating);
    CHECK(rides[5].waitTime == -1);   // queue object exists but no STANDBY
}

TEST_CASE("fetchRideData: returns false on HTTP error") {
    MockHTTP::clear();
    MockHTTP::set(LIVE_URL, "", 500);
    QueueApi api;
    RideInfo rides[MAX_RIDES]; int count = 0;
    CHECK_FALSE(api.fetchRideData(PARK_ID, TODAY, 0, rides, count, MAX_RIDES));
}

TEST_CASE("fetchRideData: returns false for an implausible parkId") {
    QueueApi api;
    RideInfo rides[MAX_RIDES]; int count = 0;
    CHECK_FALSE(api.fetchRideData("", TODAY, 0, rides, count, MAX_RIDES));
    CHECK_FALSE(api.fetchRideData("42", TODAY, 0, rides, count, MAX_RIDES));
}

TEST_CASE("fetchRideData: respects maxRides limit") {
    MockHTTP::clear();
    MockHTTP::set(LIVE_URL, LIVE_JSON);
    QueueApi api;
    RideInfo rides[2]; int count = 0;
    REQUIRE(api.fetchRideData(PARK_ID, TODAY, 0, rides, count, 2));
    CHECK(count == 2);  // capped at maxRides
}

TEST_CASE("fetchRideData: malformed JSON returns false") {
    MockHTTP::clear();
    MockHTTP::set(LIVE_URL, "{bad json");
    QueueApi api;
    RideInfo rides[MAX_RIDES]; int count = 0;
    CHECK_FALSE(api.fetchRideData(PARK_ID, TODAY, 0, rides, count, MAX_RIDES));
}

TEST_CASE("fetchRideData: chunked response (no Content-Length) still parses") {
    MockHTTP::clear();
    MockHTTP::set(LIVE_URL, LIVE_JSON);
    MockHTTP::setChunked(LIVE_URL);
    QueueApi api;
    RideInfo rides[MAX_RIDES]; int count = 0;
    REQUIRE(api.fetchRideData(PARK_ID, TODAY, 0, rides, count, MAX_RIDES));
    CHECK(count == 6);
}

TEST_CASE("fetchRideData: non-ASCII names are transliterated for the LCD font") {
    MockHTTP::clear();
    // "Crème Brûlée™" (Adjacent literals split after hex escapes: "\xADa"
    // would otherwise parse as the single escape 0xADA — MSVC C2022.)
    MockHTTP::set(LIVE_URL,
      "{\"liveData\":[{\"id\":\"aaaaaaaa-1111-2222-3333-444444444444\","
      "\"name\":\"Cr\xC3\xA8me Br\xC3\xBBl\xC3\xA9" "e\xE2\x84\xA2\","
      "\"entityType\":\"ATTRACTION\",\"status\":\"OPERATING\","
      "\"queue\":{\"STANDBY\":{\"waitTime\":10}}}]}");

    QueueApi api;
    RideInfo rides[MAX_RIDES]; int count = 0;
    REQUIRE(api.fetchRideData(PARK_ID, TODAY, 0, rides, count, MAX_RIDES));
    REQUIRE(count == 1);
    CHECK(rides[0].name == "Creme Brulee");   // accents folded, (TM) dropped
}

TEST_CASE("fetchRideData: unknown status maps to Closed") {
    MockHTTP::clear();
    MockHTTP::set(LIVE_URL, R"({"liveData":[
      {"id":"aaaaaaaa-1111-2222-3333-444444444444","name":"Mystery",
       "entityType":"ATTRACTION","status":"SOMETHING_NEW"}]})");
    QueueApi api;
    RideInfo rides[MAX_RIDES]; int count = 0;
    REQUIRE(api.fetchRideData(PARK_ID, TODAY, 0, rides, count, MAX_RIDES));
    CHECK(rides[0].status == RideStatus::Closed);
}

// ── fetchAvailableParks ───────────────────────────────────────────────────────

TEST_CASE("fetchAvailableParks: parses all parks with destination as group") {
    MockHTTP::clear();
    MockHTTP::set("https://api.themeparks.wiki/v1/destinations", DESTINATIONS_JSON);

    QueueApi api;
    std::vector<String> ids, names, groups;
    REQUIRE(api.fetchAvailableParks(ids, names, groups));
    REQUIRE(ids.size() == 3);
    CHECK(ids[0]    == PARK_ID);
    CHECK(names[0]  == "Magic Kingdom Park");
    CHECK(groups[0] == "Walt Disney World Resort");
    CHECK(names[1]  == "EPCOT");
    CHECK(groups[2] == "Efteling");
}

TEST_CASE("fetchAvailableParks: returns false on HTTP error") {
    MockHTTP::clear();
    MockHTTP::set("https://api.themeparks.wiki/v1/destinations", "", 503);
    QueueApi api;
    std::vector<String> ids, names, groups;
    CHECK_FALSE(api.fetchAvailableParks(ids, names, groups));
}

// ── getParkTimezone ───────────────────────────────────────────────────────────

TEST_CASE("getParkTimezone: reads the entity endpoint") {
    MockHTTP::clear();
    MockHTTP::set("https://api.themeparks.wiki/v1/entity/" PARK_ID,
      R"({"id":")" PARK_ID R"(","name":"Magic Kingdom Park",
          "timezone":"America/New_York","entityType":"PARK"})");
    QueueApi api;
    CHECK(api.getParkTimezone(PARK_ID) == "America/New_York");
}

TEST_CASE("getParkTimezone: unknown park returns UTC") {
    MockHTTP::clear();   // 404 for everything
    QueueApi api;
    CHECK(api.getParkTimezone("00000000-0000-0000-0000-000000000000") == "UTC");
}

TEST_CASE("getParkTimezone: caches result (second call uses cache, not HTTP)") {
    MockHTTP::clear();
    MockHTTP::set("https://api.themeparks.wiki/v1/entity/" PARK_ID,
      R"({"timezone":"America/New_York"})");
    QueueApi api;
    CHECK(api.getParkTimezone(PARK_ID) == "America/New_York");  // fetches
    MockHTTP::clear();                                          // no HTTP now
    CHECK(api.getParkTimezone(PARK_ID) == "America/New_York");  // cache hit
}

// ── normalizeId ───────────────────────────────────────────────────────────────

TEST_CASE("normalizeId: strips dashes and lowercases") {
    CHECK(QueueApi::normalizeId("AAAAAAAA-1111-2222-3333-444444444444")
          == "aaaaaaaa111122223333444444444444");
    CHECK(QueueApi::normalizeId("already-lower") == "alreadylower");
    CHECK(QueueApi::normalizeId("") == "");
    CHECK(QueueApi::normalizeId(nullptr) == "");
}

// ── getParkHours / parkClosedNow ─────────────────────────────────────────────

#define SCHED_URL "https://api.themeparks.wiki/v1/entity/" PARK_ID "/schedule"

// Real /schedule shape: TICKETED_EVENT entries (Early Entry), OPERATING with
// the big "purchases" blob the filter must drop, and multiple days.
static const char* SCHEDULE_JSON = R"({
  "schedule": [
    {"date": "2026-07-07", "type": "TICKETED_EVENT", "description": "Early Entry",
     "openingTime": "2026-07-07T08:30:00-04:00", "closingTime": "2026-07-07T09:00:00-04:00"},
    {"date": "2026-07-07", "type": "OPERATING",
     "purchases": [{"id": "ll_1", "name": "Lightning Lane", "type": "PACKAGE",
                    "price": {"amount": 3200, "currency": "USD"}, "available": true}],
     "openingTime": "2026-07-07T09:00:00-04:00", "closingTime": "2026-07-07T23:00:00-04:00"},
    {"date": "2026-07-08", "type": "OPERATING",
     "openingTime": "2026-07-08T09:00:00-04:00", "closingTime": "2026-07-08T22:00:00-04:00"}
  ]
})";

TEST_CASE("getParkHours: picks today's OPERATING entry, ignores ticketed events") {
    MockHTTP::clear();
    MockHTTP::set(SCHED_URL, SCHEDULE_JSON);
    QueueApi api;
    ParkHours h;
    REQUIRE(api.getParkHours(PARK_ID, "2026-07-07", h));
    CHECK(h.known());
    CHECK(h.openMin  == 9 * 60);    // 09:00, not the 08:30 Early Entry
    CHECK(h.closeMin == 23 * 60);
    CHECK_FALSE(h.closedAllDay());
}

TEST_CASE("getParkHours: multiple OPERATING entries merge to widest window") {
    MockHTTP::clear();
    MockHTTP::set(SCHED_URL, R"({"schedule":[
      {"date":"2026-07-07","type":"OPERATING",
       "openingTime":"2026-07-07T14:00:00-04:00","closingTime":"2026-07-07T23:00:00-04:00"},
      {"date":"2026-07-07","type":"OPERATING",
       "openingTime":"2026-07-07T09:00:00-04:00","closingTime":"2026-07-07T13:00:00-04:00"}]})");
    QueueApi api;
    ParkHours h;
    REQUIRE(api.getParkHours(PARK_ID, "2026-07-07", h));
    CHECK(h.openMin  == 9 * 60);
    CHECK(h.closeMin == 23 * 60);
}

TEST_CASE("getParkHours: closing past midnight clamps to end of day") {
    MockHTTP::clear();
    MockHTTP::set(SCHED_URL, R"({"schedule":[
      {"date":"2026-07-07","type":"OPERATING",
       "openingTime":"2026-07-07T10:00:00+02:00","closingTime":"2026-07-08T01:00:00+02:00"}]})");
    QueueApi api;
    ParkHours h;
    REQUIRE(api.getParkHours(PARK_ID, "2026-07-07", h));
    CHECK(h.openMin  == 10 * 60);
    CHECK(h.closeMin == 1440);
}

TEST_CASE("getParkHours: day without an OPERATING entry means closed all day") {
    MockHTTP::clear();
    MockHTTP::set(SCHED_URL, SCHEDULE_JSON);
    QueueApi api;
    ParkHours h;
    REQUIRE(api.getParkHours(PARK_ID, "2026-07-09", h));  // not in fixture
    CHECK(h.closedAllDay());
    CHECK_FALSE(h.known());
}

TEST_CASE("getParkHours: fetch failure returns false and leaves out untouched") {
    MockHTTP::clear();
    MockHTTP::set(SCHED_URL, "", 503);
    QueueApi api;
    ParkHours h;
    h.date = "sentinel";
    CHECK_FALSE(api.getParkHours(PARK_ID, "2026-07-07", h));
    CHECK(h.date == "sentinel");
}

TEST_CASE("getParkHours: rejects pre-NTP empty date") {
    QueueApi api;
    ParkHours h;
    CHECK_FALSE(api.getParkHours(PARK_ID, "", h));
}

TEST_CASE("getParkHours: cached per day, refetched when the date rolls over") {
    MockHTTP::clear();
    MockHTTP::set(SCHED_URL, SCHEDULE_JSON);
    QueueApi api;
    ParkHours h;
    REQUIRE(api.getParkHours(PARK_ID, "2026-07-07", h));  // fetches
    MockHTTP::clear();                                     // no HTTP now
    REQUIRE(api.getParkHours(PARK_ID, "2026-07-07", h));  // cache hit
    CHECK(h.openMin == 9 * 60);
    // New day: cache is stale, needs a fetch — which now 404s.
    CHECK_FALSE(api.getParkHours(PARK_ID, "2026-07-08", h));
    // Refetch works once the mock is back.
    MockHTTP::set(SCHED_URL, SCHEDULE_JSON);
    REQUIRE(api.getParkHours(PARK_ID, "2026-07-08", h));
    CHECK(h.closeMin == 22 * 60);
}

TEST_CASE("parkClosedNow: window boundaries") {
    ParkHours h;
    h.date = "2026-07-07"; h.openMin = 9 * 60; h.closeMin = 23 * 60;
    CHECK(parkClosedNow(h, 0));            // night
    CHECK(parkClosedNow(h, 9 * 60 - 1));   // 08:59
    CHECK_FALSE(parkClosedNow(h, 9 * 60)); // opening minute is open
    CHECK_FALSE(parkClosedNow(h, 23 * 60 - 1));
    CHECK(parkClosedNow(h, 23 * 60));      // closing minute is closed
}

TEST_CASE("parkClosedNow: unknown hours never force-close") {
    ParkHours h;                            // never fetched
    CHECK_FALSE(parkClosedNow(h, 0));
    CHECK_FALSE(parkClosedNow(h, 720));
}

TEST_CASE("parkClosedNow: closed-all-day is closed at any time") {
    ParkHours h;
    h.date = "2026-07-07";                  // fetched, no OPERATING entry
    CHECK(parkClosedNow(h, 0));
    CHECK(parkClosedNow(h, 720));
}

TEST_CASE("parkClosedNow: open-past-midnight window (closeMin 1440)") {
    ParkHours h;
    h.date = "2026-07-07"; h.openMin = 10 * 60; h.closeMin = 1440;
    CHECK(parkClosedNow(h, 9 * 60));
    CHECK_FALSE(parkClosedNow(h, 23 * 60));
    CHECK_FALSE(parkClosedNow(h, 1439));    // 23:59 still open
}

// ── device-path stream regressions ───────────────────────────────────────────
// These run the SAME wait-loop read path the device uses (the mock body
// stream is Arduino-WiFiClient-shaped, not a std::istream) — added after two
// hardware-only failures the old istream mock could never reproduce.

TEST_CASE("fetchRideData: bursty body (TLS-record-sized chunks) parses fully") {
    MockHTTP::clear();
    MockHTTP::set(LIVE_URL, LIVE_JSON);
    MockHTTP::setBursts(LIVE_URL, 97);   // odd size → bursts split tokens
    QueueApi api;
    RideInfo rides[MAX_RIDES]; int count = 0;
    REQUIRE(api.fetchRideData(PARK_ID, TODAY, 0, rides, count, MAX_RIDES));
    CHECK(count == 6);
    CHECK(rides[0].waitTime == 45);
}

TEST_CASE("fetchRideData: mid-body truncation recovers on the retry") {
    MockHTTP::clear();
    MockHTTP::set(LIVE_URL, LIVE_JSON);
    MockHTTP::setTruncateOnce(LIVE_URL, 300);   // first attempt dies mid-array
    QueueApi api;
    RideInfo rides[MAX_RIDES]; int count = 0;
    REQUIRE(api.fetchRideData(PARK_ID, TODAY, 0, rides, count, MAX_RIDES));
    CHECK(count == 6);                           // retry got the full body
    CHECK(MockHTTP::getCounts[LIVE_URL] == 2);   // exactly one retry
}

TEST_CASE("fetchRideData: truncation retry does not duplicate rides") {
    MockHTTP::clear();
    MockHTTP::set(LIVE_URL, LIVE_JSON);
    // Cut AFTER a complete element so some rides were already delivered to
    // the callback before the failure — onBegin() must reset the count.
    MockHTTP::setTruncateOnce(LIVE_URL, 900);
    QueueApi api;
    RideInfo rides[MAX_RIDES]; int count = 0;
    REQUIRE(api.fetchRideData(PARK_ID, TODAY, 0, rides, count, MAX_RIDES));
    CHECK(count == 6);
}

TEST_CASE("getParkHours: bursty schedule body parses (single-doc path)") {
    MockHTTP::clear();
    MockHTTP::set(SCHED_URL, SCHEDULE_JSON);
    MockHTTP::setBursts(SCHED_URL, 61);
    QueueApi api;
    ParkHours h;
    REQUIRE(api.getParkHours(PARK_ID, "2026-07-07", h));
    CHECK(h.openMin == 9 * 60);
}
