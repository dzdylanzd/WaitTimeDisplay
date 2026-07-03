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

// Ride sort order on the device (RuntimeConfig::sortMode)
#define SORT_MODE_API_ORDER  0
#define SORT_MODE_WAIT_DESC  1

// Number of UI colour palettes (RuntimeConfig::colorPalette, 0 = default).
// The palette definitions live in display.cpp (PALETTES[]) and their names
// in the web UI (cfgserver.cpp PALETTE_NAMES) — keep all three in sync.
#define COLOR_PALETTE_COUNT  5

struct RuntimeConfig {
  unsigned long apiRefreshInterval    = DEFAULT_API_REFRESH_INTERVAL;
  unsigned long rotateInterval        = DEFAULT_ROTATE_INTERVAL;
  unsigned long closedParkDisplayTime = DEFAULT_CLOSED_PARK_DISPLAY_TIME;
  unsigned long timeUpdateInterval    = DEFAULT_TIME_UPDATE_INTERVAL;

  std::vector<int>    enabledParkIds;
  std::vector<String> enabledParkNames;
  String              rideFiltersJson;

  // ---- Display: backlight + quiet hours + status LED ----
  uint8_t  brightness        = 100;     // 5–100 %
  bool     quietHoursEnabled = false;
  uint16_t quietStartMin     = 22 * 60; // minutes since midnight, park-local
  uint16_t quietEndMin       = 7 * 60;
  uint8_t  quietBrightness   = 0;       // 0 = backlight off during quiet hours
  bool     ledEnabled        = true;    // onboard RGB wait-colour LED
  bool     flipScreen        = false;   // rotate the display 180°
  uint8_t  colorPalette      = 0;       // UI chrome palette (0 = Magic Night)
  String   deviceTimezone;              // IANA zone for quiet hours;
                                        // "" = follow the displayed park

  // ---- Wait-level thresholds + colours (screen wait panel AND the LED) ----
  // Level i colour, indexed by (int)WaitLevel: Green/Amber/Orange/Red/Closed.
  // Defaults match the original fixed themes.
  uint8_t  waitTh1 = 15;                // "short" up to N minutes
  uint8_t  waitTh2 = 30;                // "medium" up to N minutes
  uint8_t  waitTh3 = 45;                // "long" up to N minutes; above = red
  uint32_t waitColors[5] = { 0x00E676, 0xFFD600, 0xFF7043, 0xFF1744, 0x18FFFF };

  // ---- Ride display options (global) ----
  uint8_t sortMode        = SORT_MODE_API_ORDER;
  bool    favoritesFirst  = true;
  bool    skipClosedRides = false;
  uint8_t minWaitMinutes  = 0;          // hide open rides waiting < N min
  String  rideFavoritesJson;            // {"<parkId>":[rideId,...]}
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

  void saveDisplaySettings(uint8_t brightness, bool quietEnabled,
                           uint16_t quietStartMin, uint16_t quietEndMin,
                           uint8_t quietBrightness, bool ledEnabled,
                           bool flipScreen, const String& deviceTimezone);
  void savePalette(uint8_t colorPalette);
  void saveWaitConfig(uint8_t th1, uint8_t th2, uint8_t th3,
                      const uint32_t colors[5]);
  void saveRideOptions(uint8_t sortMode, bool favoritesFirst,
                       bool skipClosedRides, uint8_t minWaitMinutes);
  void saveRideFavorites(const String& favoritesJson);

  // Wipe the entire NVS namespace (parks, filters, timings AND the WiFi
  // credentials WiFiManager keeps there) and reset to compile-time defaults.
  void factoryReset();

  const RuntimeConfig& getConfig() const { return _config; }
  bool hasEnabledParks() const { return _config.enabledParkIds.size() > 0; }

  bool parseEnabledParks(const String& json,
                         std::vector<int>& outIds,
                         std::vector<String>& outNames);

  bool isRideEnabled(int parkId, int rideId) const;
  bool isRideFavorite(int parkId, int rideId) const;

private:
  Preferences   _prefs;
  RuntimeConfig _config;

  mutable std::map<int, std::vector<int>> _rideFilterCache;
  mutable bool _rideFilterCacheValid = false;

  mutable std::map<int, std::vector<int>> _favoriteCache;
  mutable bool _favoriteCacheValid = false;

  void rebuildRideFilterCache() const;
  void rebuildFavoriteCache() const;

  static constexpr const char* NVS_NAMESPACE = "queuewatch";
};

#endif // CONFIGMANAGER_H
