#ifndef QUEUEAPI_H
#define QUEUEAPI_H

#include <Arduino.h>
#include <ArduinoJson.h>

struct RideInfo {
  int    id = -1;
  String name;
  String land;              // themed area name ("" when the park has no lands)
  int    waitTime = -1;
  bool   isOpen = false;

  // Annotations added after fetch (AppStateManager), not parsed from the API:
  int8_t  trend      = 0;   // -1 falling / 0 flat / +1 rising vs last refresh
  int16_t trendDelta = 0;   // signed minutes behind the arrow
  bool    favorite   = false;
};

class QueueApi {
public:
  QueueApi();

  String getParkTimezone(int parkId);
  String getParkCountry(int parkId);   // ASCII-folded, "" when unknown

  // Returns true and populates rideCount/rides[] with up to maxRides entries
  // on success. On failure (false), rideCount/rides[] are left UNTOUCHED (not
  // zeroed) — callers must decide their own "keep stale data" vs "clear it"
  // policy for a failed fetch; this function doesn't decide it for them.
  bool fetchRideData(int parkId,
                     RideInfo rides[], int& rideCount, int maxRides);

  bool fetchAvailableParks(std::vector<int>& outIds,
                           std::vector<String>& outNames,
                           std::vector<String>& outGroups);

private:
  struct TZCache {
    int    parkId = -1;
    String tz;
    String country;
  };
  static constexpr int TZ_CACHE_SIZE = 20;
  TZCache _tzCache[TZ_CACHE_SIZE];
  int     _tzCacheCount = 0;

  // Built once in the constructor and reused by fetchParksDoc() — the
  // parks.json filter shape never changes between calls.
  StaticJsonDocument<256> _parksFilter;

  const TZCache* lookupPark(int parkId);
  bool fetchParksDoc(DynamicJsonDocument& doc);
  bool httpGetJson(const String& url, DynamicJsonDocument& doc,
                   JsonDocument* filter = nullptr);
  static void appendRide(JsonObject ride, const char* landName,
                         RideInfo rides[], int& rideCount);
};

#endif // QUEUEAPI_H
