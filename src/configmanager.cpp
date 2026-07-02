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
  _prefs.end();

  if (parksJson.length() > 0) {
    parseEnabledParks(parksJson, _config.enabledParkIds, _config.enabledParkNames);
  }
  _rideFilterCacheValid = false;
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

void ConfigManager::factoryReset() {
  // WiFiManager stores its credentials in the same namespace, so clearing
  // it wipes those too — that's the point of a factory reset.
  _prefs.begin(NVS_NAMESPACE, false);
  _prefs.clear();
  _prefs.end();
  _config = RuntimeConfig();
  _rideFilterCache.clear();
  _rideFilterCacheValid = false;
}

bool ConfigManager::parseEnabledParks(const String& json,
                                       std::vector<int>& outIds,
                                       std::vector<String>& outNames) {
  outIds.clear();
  outNames.clear();
  DynamicJsonDocument doc(4096);
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
  DynamicJsonDocument doc(4096);
  DeserializationError err = deserializeJson(doc, _config.rideFiltersJson);
  if (err) {
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
