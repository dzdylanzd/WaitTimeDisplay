#ifndef CONFIGMANAGER_H
#define CONFIGMANAGER_H

#include <Arduino.h>
#include <Preferences.h>
#include <vector>
#include <map>

// ----------- Timing defaults (ms) -----------
#define DEFAULT_API_REFRESH_INTERVAL    900000UL  // 15 min
#define DEFAULT_ROTATE_INTERVAL           10000UL  // 10 s
#define DEFAULT_CLOSED_PARK_DISPLAY_TIME  20000UL  // 20 s
#define DEFAULT_TIME_UPDATE_INTERVAL      20000UL  // 20 s (clock shows HH:MM)

struct RuntimeConfig {
  unsigned long apiRefreshInterval    = DEFAULT_API_REFRESH_INTERVAL;
  unsigned long rotateInterval        = DEFAULT_ROTATE_INTERVAL;
  unsigned long closedParkDisplayTime = DEFAULT_CLOSED_PARK_DISPLAY_TIME;
  unsigned long timeUpdateInterval    = DEFAULT_TIME_UPDATE_INTERVAL;

  std::vector<int>    enabledParkIds;
  std::vector<String> enabledParkNames;
  String              rideFiltersJson;
};

class ConfigManager {
public:
  ConfigManager();

  void load();

  void saveTimings(unsigned long apiRefresh,
                   unsigned long rotate,
                   unsigned long closedPark,
                   unsigned long timeUpdate);

  void saveEnabledParks(const String& parksJson);
  void saveRideFilters(const String& filtersJson);

  // Wipe the entire NVS namespace (parks, filters, timings AND the WiFi
  // credentials WiFiManager keeps there) and reset to compile-time defaults.
  void factoryReset();

  const RuntimeConfig& getConfig() const { return _config; }
  bool hasEnabledParks() const { return _config.enabledParkIds.size() > 0; }

  bool parseEnabledParks(const String& json,
                         std::vector<int>& outIds,
                         std::vector<String>& outNames);

  bool isRideEnabled(int parkId, int rideId) const;

private:
  Preferences   _prefs;
  RuntimeConfig _config;

  mutable std::map<int, std::vector<int>> _rideFilterCache;
  mutable bool _rideFilterCacheValid = false;

  void rebuildRideFilterCache() const;

  static constexpr const char* NVS_NAMESPACE = "queuewatch";
};

#endif // CONFIGMANAGER_H
