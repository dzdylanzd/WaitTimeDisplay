#ifndef CONFIGMANAGER_H
#define CONFIGMANAGER_H

#include <Arduino.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <vector>
#include <map>
#include "waitdefaults.h"
#include "config.h"

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
// in the web UI (cfgserver.cpp PALETTE_DEFS) — keep all three in sync. The
// LAST index is the user-defined "Custom" palette (colours below), not a
// PALETTES[] entry — see CUSTOM_PALETTE_INDEX in display.cpp.
#define COLOR_PALETTE_COUNT  22

// Max length of ONE park's stored ride-selection string (see configmanager
// .cpp: each park gets its own NVS key holding concatenated 8-hex-char ride
// id hashes). NVS strings cap at ~4000 bytes; 3800 leaves margin — that's
// 475 hashed ride ids per park, i.e. unreachable in practice (MAX_RIDES is
// 80). The real bound is total NVS space (20 KB partition ≈ 500 entries):
// a fully-filtered 60-ride park costs ~16 entries, so ~12 such parks fit
// alongside the rest of the config. Saves that don't fit are rejected with
// an error, never truncated.
#define NVS_JSON_MAX  3800

// Bumped when stored park/ride config becomes incompatible with the current
// firmware. v2 = themeparks.wiki UUID ids (old numeric queue-times ids are
// meaningless and get wiped on first load — WiFi/display settings survive).
// v3 = per-park hashed ride filters/favorites (v2's single ride_flt/ride_fav
// JSON blobs are wiped; enabled parks survive, filters are re-picked once).
#define CONFIG_VERSION 3

struct RuntimeConfig {
  unsigned long apiRefreshInterval    = DEFAULT_API_REFRESH_INTERVAL;
  unsigned long rotateInterval        = DEFAULT_ROTATE_INTERVAL;
  unsigned long closedParkDisplayTime = DEFAULT_CLOSED_PARK_DISPLAY_TIME;
  unsigned long timeUpdateInterval    = DEFAULT_TIME_UPDATE_INTERVAL;

  std::vector<String> enabledParkIds;   // dashed themeparks.wiki UUIDs
  std::vector<String> enabledParkNames;

  // ---- Display: backlight + quiet hours + status LED ----
  uint8_t  brightness        = 100;     // 5–100 %
  bool     quietHoursEnabled = false;
  uint16_t quietStartMin     = 22 * 60; // minutes since midnight, park-local
  uint16_t quietEndMin       = 7 * 60;
  uint8_t  quietBrightness   = 0;       // 0 = backlight off during quiet hours
  bool     ledEnabled        = true;    // onboard RGB wait-colour LED
  bool     flipScreen        = false;   // rotate the display 180°
  uint8_t  colorPalette      = 0;       // UI chrome palette (0 = Magic Night,
                                        // COLOR_PALETTE_COUNT-1 = Custom)
  // User-defined "Custom" palette: 3 picked 0xRRGGBB colours (header bg,
  // accent, ride-panel bg); the rest of the palette is derived from these.
  // Defaults mirror Magic Night so a fresh Custom palette looks sensible.
  uint32_t customHdr    = 0x2A0860;
  uint32_t customAccent = 0xC89E20;
  uint32_t customPanel  = 0x160A34;
  String   deviceTimezone;              // IANA zone for quiet hours;
                                        // "" = follow the displayed park

  // ---- Wait-level thresholds + colours (screen wait panel AND the LED) ----
  // Level i colour, indexed by (int)WaitLevel: Green/Amber/Orange/Red/Closed.
  // Defaults come from waitdefaults.h (the single source for all consumers).
  uint8_t  waitTh1 = WAIT_TH_DEFAULTS[0];   // "short" up to N minutes
  uint8_t  waitTh2 = WAIT_TH_DEFAULTS[1];   // "medium" up to N minutes
  uint8_t  waitTh3 = WAIT_TH_DEFAULTS[2];   // "long" up to N minutes; above = red
  uint32_t waitColors[5] = {
    WAIT_COLOR_DEFAULTS[0], WAIT_COLOR_DEFAULTS[1], WAIT_COLOR_DEFAULTS[2],
    WAIT_COLOR_DEFAULTS[3], WAIT_COLOR_DEFAULTS[4]
  };

  // ---- Ride display options (global) ----
  uint8_t sortMode        = SORT_MODE_API_ORDER;
  bool    favoritesFirst  = true;
  bool    skipClosedRides = false;
  uint8_t minWaitMinutes  = 0;          // hide open rides waiting < N min
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

  // Merge-apply the web UI's ride selections. `filters`/`favorites` map
  // dashed park UUIDs to arrays of ride ids (32-char undashed, or 8-hex
  // hashes when re-importing an exported config); a null value clears that
  // park's entry, an absent park keeps whatever is stored. Entries for
  // parks not in keepParkIds are removed. Each park is stored under its own
  // NVS key as concatenated 8-hex fnv1a32 hashes — no shared size budget,
  // so every configured park can carry a full filter. Returns false (and
  // sets outError) when a park's string exceeds NVS_JSON_MAX or NVS is out
  // of space; already-written parks stay written (per-park atomicity).
  bool applyRideSelections(JsonObjectConst filters, JsonObjectConst favorites,
                           const std::vector<String>& keepParkIds,
                           String& outError);

  // Fill two JSON objects ({"<parkUuid>":["hex8",...]}) with the stored
  // selections — used by GET /api/config for the export file.
  void exportRideSelections(JsonObject outFilters, JsonObject outFavorites) const;

  void saveDisplaySettings(uint8_t brightness, bool quietEnabled,
                           uint16_t quietStartMin, uint16_t quietEndMin,
                           uint8_t quietBrightness, bool ledEnabled,
                           bool flipScreen, const String& deviceTimezone);
  void savePalette(uint8_t colorPalette);
  void saveCustomPalette(uint32_t hdr, uint32_t accent, uint32_t panel);
  void saveWaitConfig(uint8_t th1, uint8_t th2, uint8_t th3,
                      const uint32_t colors[5]);
  void saveRideOptions(uint8_t sortMode, bool favoritesFirst,
                       bool skipClosedRides, uint8_t minWaitMinutes);

  // Wipe the entire NVS namespace (parks, filters, timings AND the WiFi
  // credentials WiFiManager keeps there) and reset to compile-time defaults.
  void factoryReset();

  const RuntimeConfig& getConfig() const { return _config; }
  bool hasEnabledParks() const { return _config.enabledParkIds.size() > 0; }

  bool parseEnabledParks(const String& json,
                         std::vector<String>& outIds,
                         std::vector<String>& outNames);

  bool isRideEnabled(const String& parkId, const String& rideId) const;
  bool isRideFavorite(const String& parkId, const String& rideId) const;

private:
  mutable Preferences _prefs;   // lazy selection reads happen in const getters
  RuntimeConfig _config;

  // Per-park selection strings (concatenated 8-hex ride-id hashes), lazily
  // loaded from their NVS keys. An entry with an empty string means "no key
  // stored" (filter: all rides enabled / favorites: none).
  mutable std::map<String, String> _filterCache;
  mutable std::map<String, String> _favoriteCache;

  // Dashed UUIDs of parks that own per-park NVS keys (NVS can't enumerate
  // keys through Preferences, so this index makes cleanup possible).
  std::vector<String> _selectionIndex;

  const String& cachedSelection(char kind, const String& parkId,
                                std::map<String, String>& cache) const;
  void loadSelectionIndex();
  void storeSelectionIndex();
};

#endif // CONFIGMANAGER_H
