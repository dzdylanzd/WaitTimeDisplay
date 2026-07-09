#ifndef QUEUEAPI_H
#define QUEUEAPI_H

#include <Arduino.h>
#include <ArduinoJson.h>

// Live entity status from themeparks.wiki (liveData[].status). Anything the
// API reports that isn't recognised maps to Closed.
// (Mixed-case values on purpose — ALL-CAPS names collide with Windows macros
//  in the simulator build.)
enum class RideStatus : uint8_t { Operating, Down, Closed, Refurbishment };

// liveData[].entityType, reduced to the two kinds QueueWatch displays.
enum class EntityKind : uint8_t { Attraction, Show };

struct RideInfo {
  String     id;                  // 32-char undashed lowercase UUID, "" = invalid
  String     name;
  RideStatus status = RideStatus::Closed;
  EntityKind kind   = EntityKind::Attraction;
  int        waitTime = -1;       // STANDBY queue minutes; -1 = unknown / n.a.
  int16_t    nextShowMin = -1;    // shows: next startTime today as minutes-of-day
                                  // (park-local); -1 = no further show today

  // Annotations added after fetch (AppStateManager), not parsed from the API:
  int8_t  trend      = 0;   // -1 falling / 0 flat / +1 rising vs last refresh
  int16_t trendDelta = 0;   // signed minutes behind the arrow
  bool    favorite   = false;

  bool isOpen() const { return status == RideStatus::Operating; }
};

// Today's operating window for a park, from /v1/entity/{id}/schedule.
// date is the day the entry applies to ("" until a successful fetch).
// A successfully fetched day WITHOUT an OPERATING entry keeps openMin/
// closeMin at -1 — the park is closed all day.
struct ParkHours {
  String date;           // "YYYY-MM-DD"; "" = never fetched / fetch failed
  int    openMin  = -1;  // park-local minutes-of-day
  int    closeMin = -1;  // 1440 when the park closes past midnight

  bool known()        const { return date.length() == 10 && openMin >= 0; }
  bool closedAllDay() const { return date.length() == 10 && openMin < 0; }
};

// Pure schedule check (unit-tested): is the park outside its operating
// window right now? Unknown hours → false, never force-close without data.
inline bool parkClosedNow(const ParkHours& h, int nowMin) {
  if (h.closedAllDay()) return true;
  if (!h.known()) return false;
  if (h.openMin >= h.closeMin) return false;  // degenerate entry — trust live data
  return nowMin < h.openMin || nowMin >= h.closeMin;
}

class QueueApi {
public:
  String getParkTimezone(const String& parkId);

  // Today's operating hours from /v1/entity/{parkId}/schedule, cached per
  // park per day (a new todayDate refetches). Returns false — leaving `out`
  // untouched — on fetch failure or when todayDate isn't a full date yet
  // (pre-NTP); callers keep their existing all-rides-closed fallback then.
  bool getParkHours(const String& parkId, const String& todayDate,
                    ParkHours& out);

  // Fetch /v1/entity/{parkId}/live and populate rides[] with ATTRACTION and
  // SHOW entities (everything else is skipped). todayDate ("YYYY-MM-DD") and
  // nowMin (minutes-of-day, park-local) drive next-showtime selection — the
  // API's timestamps carry the park-local UTC offset, so this is pure string
  // matching. outParkStatus (optional) receives the park's own live status.
  //
  // Returns true and populates rideCount/rides[] with up to maxRides entries
  // on success. On failure (false), rides[]/rideCount may hold a PARTIAL
  // result — the body is stream-parsed element by element, so callers that
  // need stale data to survive a failed refresh must fetch into a scratch
  // array (see AppStateManager::fetchAndProcessRideData).
  bool fetchRideData(const String& parkId,
                     const String& todayDate, int nowMin,
                     RideInfo rides[], int& rideCount, int maxRides,
                     RideStatus* outParkStatus = nullptr);

  bool fetchAvailableParks(std::vector<String>& outIds,
                           std::vector<String>& outNames,
                           std::vector<String>& outGroups);

  // Strip dashes + lowercase a UUID. Ride ids are stored/compared in this
  // compact form (park ids keep their dashed form — they go into URLs).
  static String normalizeId(const char* uuid);

private:
  struct TZCache {
    String parkId;                // dashed UUID, "" = empty slot
    String tz;
  };
  static constexpr int TZ_CACHE_SIZE = 20;
  TZCache _tzCache[TZ_CACHE_SIZE];
  int     _tzCacheCount = 0;
  int     _tzCacheNext  = 0;   // round-robin write cursor once full

  const TZCache* lookupPark(const String& parkId);

  // Hours cache: one entry per park, refreshed when the date rolls over.
  struct HoursCache {
    String    parkId;             // dashed UUID, "" = empty slot
    ParkHours hours;
  };
  static constexpr int HOURS_CACHE_SIZE = 20;
  HoursCache _hoursCache[HOURS_CACHE_SIZE];
  int        _hoursCacheCount = 0;
  int        _hoursCacheNext  = 0;   // round-robin write cursor once full
};

#endif // QUEUEAPI_H
