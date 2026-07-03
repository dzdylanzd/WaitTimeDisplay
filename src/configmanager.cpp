#include "configmanager.h"
#include <ArduinoJson.h>

ConfigManager::ConfigManager() {}

void ConfigManager::load() {
  _prefs.begin(NVS_NAMESPACE, true);
  _config.apiRefreshInterval    = _prefs.getULong("api_int",    DEFAULT_API_REFRESH_INTERVAL);
  _config.rotateInterval        = _prefs.getULong("rot_int",    DEFAULT_ROTATE_INTERVAL);
  _config.closedParkDisplayTime = _prefs.getULong("closed_int", DEFAULT_CLOSED_PARK_DISPLAY_TIME);
  _config.timeUpdateInterval    = _prefs.getULong("time_int",   DEFAULT_TIME_UPDATE_INTERVAL);
  _config.rideFiltersJson       = _prefs.getString("ride_flt",  "");
  String parksJson              = _prefs.getString("enabled_pks", "");

  _config.brightness        = (uint8_t)_prefs.getInt("brt", 100);
  if (_config.brightness < 5 || _config.brightness > 100) _config.brightness = 100;
  _config.quietHoursEnabled = _prefs.getBool("qt_en", false);
  _config.quietStartMin     = (uint16_t)_prefs.getInt("qt_sta", 22 * 60);
  if (_config.quietStartMin > 1439) _config.quietStartMin = 22 * 60;
  _config.quietEndMin       = (uint16_t)_prefs.getInt("qt_end", 7 * 60);
  if (_config.quietEndMin > 1439) _config.quietEndMin = 7 * 60;
  _config.quietBrightness   = (uint8_t)_prefs.getInt("qt_brt", 0);
  if (_config.quietBrightness > 100) _config.quietBrightness = 0;
  _config.ledEnabled        = _prefs.getBool("led_en", true);
  _config.flipScreen        = _prefs.getBool("flip_scr", false);
  _config.colorPalette      = (uint8_t)_prefs.getInt("pal", 0);
  if (_config.colorPalette >= COLOR_PALETTE_COUNT) _config.colorPalette = 0;

  _config.waitTh1 = (uint8_t)_prefs.getInt("wt1", WAIT_TH_DEFAULTS[0]);
  _config.waitTh2 = (uint8_t)_prefs.getInt("wt2", WAIT_TH_DEFAULTS[1]);
  _config.waitTh3 = (uint8_t)_prefs.getInt("wt3", WAIT_TH_DEFAULTS[2]);
  for (int i = 0; i < 5; i++) {
    char key[4] = { 'w', 'c', (char)('0' + i), '\0' };
    _config.waitColors[i] =
        (uint32_t)_prefs.getInt(key, (int32_t)WAIT_COLOR_DEFAULTS[i]);
  }
  _config.deviceTimezone    = _prefs.getString("dev_tz", "");

  _config.sortMode          = (uint8_t)_prefs.getInt("sort_mode", SORT_MODE_API_ORDER);
  if (_config.sortMode > SORT_MODE_WAIT_DESC) _config.sortMode = SORT_MODE_API_ORDER;
  _config.favoritesFirst    = _prefs.getBool("fav_first", true);
  _config.skipClosedRides   = _prefs.getBool("skip_closed", false);
  _config.minWaitMinutes    = (uint8_t)_prefs.getInt("min_wait", 0);
  _config.rideFavoritesJson = _prefs.getString("ride_fav", "");
  _prefs.end();

  if (parksJson.length() > 0) {
    parseEnabledParks(parksJson, _config.enabledParkIds, _config.enabledParkNames);
  }
  _rideFilterCacheValid = false;
  _favoriteCacheValid   = false;
}

void ConfigManager::saveTimings(unsigned long apiRefresh,
                                 unsigned long rotate,
                                 unsigned long closedPark,
                                 unsigned long timeUpdate) {
  _prefs.begin(NVS_NAMESPACE, false);
  _prefs.putULong("api_int",    apiRefresh);
  _prefs.putULong("rot_int",    rotate);
  _prefs.putULong("closed_int", closedPark);
  _prefs.putULong("time_int",   timeUpdate);
  _prefs.end();
  _config.apiRefreshInterval    = apiRefresh;
  _config.rotateInterval        = rotate;
  _config.closedParkDisplayTime = closedPark;
  _config.timeUpdateInterval    = timeUpdate;
}

void ConfigManager::saveEnabledParks(const String& parksJson) {
  _prefs.begin(NVS_NAMESPACE, false);
  _prefs.putString("enabled_pks", parksJson);
  _prefs.end();
  _config.enabledParkIds.clear();
  _config.enabledParkNames.clear();
  parseEnabledParks(parksJson, _config.enabledParkIds, _config.enabledParkNames);
}

void ConfigManager::saveRideFilters(const String& filtersJson) {
  _prefs.begin(NVS_NAMESPACE, false);
  _prefs.putString("ride_flt", filtersJson);
  _prefs.end();
  _config.rideFiltersJson = filtersJson;
  _rideFilterCacheValid = false;
}

void ConfigManager::saveDisplaySettings(uint8_t brightness, bool quietEnabled,
                                         uint16_t quietStartMin, uint16_t quietEndMin,
                                         uint8_t quietBrightness, bool ledEnabled,
                                         bool flipScreen, const String& deviceTimezone) {
  _prefs.begin(NVS_NAMESPACE, false);
  _prefs.putInt("brt",       brightness);
  _prefs.putBool("qt_en",    quietEnabled);
  _prefs.putInt("qt_sta",    quietStartMin);
  _prefs.putInt("qt_end",    quietEndMin);
  _prefs.putInt("qt_brt",    quietBrightness);
  _prefs.putBool("led_en",   ledEnabled);
  _prefs.putBool("flip_scr", flipScreen);
  _prefs.putString("dev_tz", deviceTimezone);
  _prefs.end();
  _config.brightness        = brightness;
  _config.quietHoursEnabled = quietEnabled;
  _config.quietStartMin     = quietStartMin;
  _config.quietEndMin       = quietEndMin;
  _config.quietBrightness   = quietBrightness;
  _config.ledEnabled        = ledEnabled;
  _config.flipScreen        = flipScreen;
  _config.deviceTimezone    = deviceTimezone;
}

void ConfigManager::savePalette(uint8_t colorPalette) {
  if (colorPalette >= COLOR_PALETTE_COUNT) colorPalette = 0;
  _prefs.begin(NVS_NAMESPACE, false);
  _prefs.putInt("pal", colorPalette);
  _prefs.end();
  _config.colorPalette = colorPalette;
}

void ConfigManager::saveWaitConfig(uint8_t th1, uint8_t th2, uint8_t th3,
                                    const uint32_t colors[5]) {
  _prefs.begin(NVS_NAMESPACE, false);
  _prefs.putInt("wt1", th1);
  _prefs.putInt("wt2", th2);
  _prefs.putInt("wt3", th3);
  for (int i = 0; i < 5; i++) {
    char key[4] = { 'w', 'c', (char)('0' + i), '\0' };
    _prefs.putInt(key, (int32_t)colors[i]);
  }
  _prefs.end();
  _config.waitTh1 = th1;
  _config.waitTh2 = th2;
  _config.waitTh3 = th3;
  for (int i = 0; i < 5; i++) _config.waitColors[i] = colors[i];
}

void ConfigManager::saveRideOptions(uint8_t sortMode, bool favoritesFirst,
                                     bool skipClosedRides, uint8_t minWaitMinutes) {
  _prefs.begin(NVS_NAMESPACE, false);
  _prefs.putInt("sort_mode",    sortMode);
  _prefs.putBool("fav_first",   favoritesFirst);
  _prefs.putBool("skip_closed", skipClosedRides);
  _prefs.putInt("min_wait",     minWaitMinutes);
  _prefs.end();
  _config.sortMode        = sortMode;
  _config.favoritesFirst  = favoritesFirst;
  _config.skipClosedRides = skipClosedRides;
  _config.minWaitMinutes  = minWaitMinutes;
}

void ConfigManager::saveRideFavorites(const String& favoritesJson) {
  _prefs.begin(NVS_NAMESPACE, false);
  _prefs.putString("ride_fav", favoritesJson);
  _prefs.end();
  _config.rideFavoritesJson = favoritesJson;
  _favoriteCacheValid = false;
}

void ConfigManager::factoryReset() {
  // WiFiManager stores its credentials in the same namespace, so clearing
  // it wipes those too — that's the point of a factory reset.
  _prefs.begin(NVS_NAMESPACE, false);
  _prefs.clear();
  _prefs.end();
  _config = RuntimeConfig();
  _rideFilterCache.clear();
  _rideFilterCacheValid = false;
  _favoriteCache.clear();
  _favoriteCacheValid = false;
}

bool ConfigManager::parseEnabledParks(const String& json,
                                       std::vector<int>& outIds,
                                       std::vector<String>& outNames) {
  outIds.clear();
  outNames.clear();
  // enabledParks isn't budget-capped like the per-park filter/favorite JSON,
  // so size generously off the actual input rather than a fixed guess.
  DynamicJsonDocument doc(json.length() * 2 + 512);
  DeserializationError err = deserializeJson(doc, json);
  if (err) {
    Serial.printf("ConfigManager: failed to parse enabledParks JSON: %s\n", err.c_str());
    return false;
  }
  JsonArray arr = doc.as<JsonArray>();
  for (JsonObject obj : arr) {
    int id = obj["id"] | -1;
    const char* name = obj["name"] | "";
    if (id > 0) {
      outIds.push_back(id);
      outNames.push_back(String(name));
    }
  }
  return outIds.size() > 0;
}

void ConfigManager::rebuildRideFilterCache() const {
  _rideFilterCache.clear();
  if (_config.rideFiltersJson.length() == 0) {
    _rideFilterCacheValid = true;
    return;
  }
  DynamicJsonDocument doc(NVS_JSON_MAX * 3);
  DeserializationError err = deserializeJson(doc, _config.rideFiltersJson);
  if (err) {
    Serial.printf("ConfigManager: failed to parse rideFiltersJson: %s\n", err.c_str());
    _rideFilterCacheValid = true;
    return;
  }
  JsonObject filters = doc.as<JsonObject>();
  for (JsonPair kv : filters) {
    int parkId = atoi(kv.key().c_str());
    JsonArray enabledIds = kv.value().as<JsonArray>();
    std::vector<int> ids;
    for (JsonVariant v : enabledIds) ids.push_back((int)v);
    _rideFilterCache[parkId] = ids;
  }
  _rideFilterCacheValid = true;
}

bool ConfigManager::isRideEnabled(int parkId, int rideId) const {
  if (!_rideFilterCacheValid) rebuildRideFilterCache();
  if (_rideFilterCache.empty()) return true;
  auto it = _rideFilterCache.find(parkId);
  if (it == _rideFilterCache.end()) return true;
  const std::vector<int>& enabledIds = it->second;
  for (size_t i = 0; i < enabledIds.size(); i++) {
    if (enabledIds[i] == rideId) return true;
  }
  return false;
}

// Same per-park JSON shape as the ride filter ({"parkId":[rideIds]}), but the
// semantics differ: absence means "not a favorite", never "all favorites".
void ConfigManager::rebuildFavoriteCache() const {
  _favoriteCache.clear();
  if (_config.rideFavoritesJson.length() == 0) {
    _favoriteCacheValid = true;
    return;
  }
  DynamicJsonDocument doc(NVS_JSON_MAX * 3);
  DeserializationError err = deserializeJson(doc, _config.rideFavoritesJson);
  if (err) {
    Serial.printf("ConfigManager: failed to parse rideFavoritesJson: %s\n", err.c_str());
    _favoriteCacheValid = true;
    return;
  }
  JsonObject favorites = doc.as<JsonObject>();
  for (JsonPair kv : favorites) {
    int parkId = atoi(kv.key().c_str());
    JsonArray favIds = kv.value().as<JsonArray>();
    std::vector<int> ids;
    for (JsonVariant v : favIds) ids.push_back((int)v);
    _favoriteCache[parkId] = ids;
  }
  _favoriteCacheValid = true;
}

bool ConfigManager::isRideFavorite(int parkId, int rideId) const {
  if (!_favoriteCacheValid) rebuildFavoriteCache();
  auto it = _favoriteCache.find(parkId);
  if (it == _favoriteCache.end()) return false;
  const std::vector<int>& favIds = it->second;
  for (size_t i = 0; i < favIds.size(); i++) {
    if (favIds[i] == rideId) return true;
  }
  return false;
}
