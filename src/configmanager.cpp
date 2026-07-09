#include "configmanager.h"
#include "idhash.h"
#include <ArduinoJson.h>

ConfigManager::ConfigManager() {}

// ---- Per-park ride-selection storage ---------------------------------------
// Each park's filter (enabled ride ids) and favorites live under their own
// NVS keys — "rf_XXXXXXXX" / "fv_XXXXXXXX", where XXXXXXXX is the fnv1a32 of
// the dashed park UUID — as a plain concatenation of 8-hex-char fnv1a32
// hashes of the (32-char undashed) ride ids. No shared budget, no JSON
// overhead: a fully-filtered 80-ride park is 640 bytes. Collisions within a
// park (≤ MAX_RIDES ids against a 32-bit space) are negligible.

static String hex8(uint32_t v) {
  char buf[9];
  snprintf(buf, sizeof(buf), "%08x", (unsigned)v);
  return String(buf);
}

// "rf_"/"fv_" + hashed park id (NVS keys max 15 chars — this is 11).
static String selKey(char kind, const String& parkId) {
  return String(kind == 'f' ? "rf_" : "fv_") + hex8(fnv1a32(parkId.c_str()));
}

// A ride id arriving from the web UI is a 32-char undashed UUID; one coming
// back from an exported config file is already an 8-hex hash. Store both as
// the hash so exports re-import losslessly. (Only an exact 8-char value is
// treated as pre-hashed — anything else gets hashed, whatever its length.)
static String rideHash(const char* id) {
  if (strlen(id) == 8) return String(id);   // already hashed (config import)
  return hex8(fnv1a32(id));
}

// True when the 8-hex hash of rideId appears in the concatenated string.
static bool selectionContains(const String& sel, const String& rideId) {
  String h = hex8(fnv1a32(rideId.c_str()));
  for (unsigned i = 0; i + 8 <= sel.length(); i += 8) {
    if (strncmp(sel.c_str() + i, h.c_str(), 8) == 0) return true;
  }
  return false;
}

void ConfigManager::load() {
  // Open read-write: the version migration below may need to delete keys.
  _prefs.begin(NVS_NAMESPACE, false);

  // Config-version migrations. Pre-v2 park/ride config used queue-times.com
  // numeric ids, which mean nothing to the themeparks.wiki API — wipe parks
  // AND filters (the user re-picks once). v2→v3 changed only the filter/
  // favorite storage (single JSON blob → per-park hashed keys), so enabled
  // parks survive and just the old blobs are dropped. WiFi credentials,
  // brightness, palette, quiet hours and timings always survive.
  int ver = _prefs.getInt("cfg_ver", 1);
  if (ver < CONFIG_VERSION) {
    if (ver < 2 && _prefs.isKey("enabled_pks")) _prefs.remove("enabled_pks");
    if (_prefs.isKey("ride_flt")) _prefs.remove("ride_flt");
    if (_prefs.isKey("ride_fav")) _prefs.remove("ride_fav");
    _prefs.putInt("cfg_ver", CONFIG_VERSION);
  }

  _config.apiRefreshInterval    = _prefs.getULong("api_int",    DEFAULT_API_REFRESH_INTERVAL);
  _config.rotateInterval        = _prefs.getULong("rot_int",    DEFAULT_ROTATE_INTERVAL);
  _config.closedParkDisplayTime = _prefs.getULong("closed_int", DEFAULT_CLOSED_PARK_DISPLAY_TIME);
  _config.timeUpdateInterval    = _prefs.getULong("time_int",   DEFAULT_TIME_UPDATE_INTERVAL);
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
  _config.customHdr    = (uint32_t)_prefs.getInt("cst_h", (int32_t)_config.customHdr);
  _config.customAccent = (uint32_t)_prefs.getInt("cst_a", (int32_t)_config.customAccent);
  _config.customPanel  = (uint32_t)_prefs.getInt("cst_p", (int32_t)_config.customPanel);

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
  loadSelectionIndex();
  _prefs.end();

  if (parksJson.length() > 0) {
    parseEnabledParks(parksJson, _config.enabledParkIds, _config.enabledParkNames);
  }
  _filterCache.clear();
  _favoriteCache.clear();
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

// The index of parks that own per-park selection keys, stored as the parks'
// dashed UUIDs concatenated (36 chars each — no JSON, fixed stride).
void ConfigManager::loadSelectionIndex() {
  _selectionIndex.clear();
  String idx = _prefs.getString("sel_idx", "");
  for (unsigned i = 0; i + 36 <= idx.length(); i += 36)
    _selectionIndex.push_back(idx.substring(i, i + 36));
}

void ConfigManager::storeSelectionIndex() {
  String idx;
  for (size_t i = 0; i < _selectionIndex.size(); i++) idx += _selectionIndex[i];
  if (idx.length() == 0) _prefs.remove("sel_idx");
  else                   _prefs.putString("sel_idx", idx);
}

bool ConfigManager::applyRideSelections(JsonObjectConst filters,
                                        JsonObjectConst favorites,
                                        const std::vector<String>& keepParkIds,
                                        String& outError) {
  outError = "";
  _prefs.begin(NVS_NAMESPACE, false);

  bool ok = true;
  bool wroteAny = false;
  // Two passes over (kind, incoming-object): identical handling.
  const char kinds[2] = { 'f', 'v' };
  JsonObjectConst objs[2] = { filters, favorites };
  for (int k = 0; k < 2 && ok; k++) {
    if (objs[k].isNull()) continue;
    for (JsonPairConst kv : objs[k]) {
      String parkId(kv.key().c_str());
      if (parkId.length() != 36) continue;          // not a dashed UUID
      bool keep = false;
      for (size_t i = 0; i < keepParkIds.size(); i++)
        if (keepParkIds[i] == parkId) { keep = true; break; }
      if (!keep) continue;                          // cleanup pass handles it

      String key = selKey(kinds[k], parkId);
      if (kv.value().isNull()) {                    // null = clear this entry
        if (_prefs.isKey(key.c_str())) _prefs.remove(key.c_str());
        continue;
      }
      String sel;
      for (JsonVariantConst v : kv.value().as<JsonArrayConst>()) {
        const char* id = v.as<const char*>();
        if (id && *id) sel += rideHash(id);
      }
      if (sel.length() == 0) {                      // empty list = no entry
        if (_prefs.isKey(key.c_str())) _prefs.remove(key.c_str());
        continue;
      }
      if (sel.length() > NVS_JSON_MAX) {            // 475 rides — unreachable
        outError = "Ride selection too large for park";
        ok = false;
        break;
      }
      if (_prefs.putString(key.c_str(), sel) != sel.length()) {
        outError = wroteAny
            ? "Device storage is full - some selections were already saved before this failure, reduce per-ride filters"
            : "Device storage is full - reduce per-ride filters";
        ok = false;
        break;
      }
      wroteAny = true;
      bool indexed = false;
      for (size_t i = 0; i < _selectionIndex.size(); i++)
        if (_selectionIndex[i] == parkId) { indexed = true; break; }
      if (!indexed) _selectionIndex.push_back(parkId);
    }
  }

  // Cleanup: drop keys of parks that are no longer enabled.
  std::vector<String> newIndex;
  for (size_t i = 0; i < _selectionIndex.size(); i++) {
    const String& parkId = _selectionIndex[i];
    bool keep = false;
    for (size_t j = 0; j < keepParkIds.size(); j++)
      if (keepParkIds[j] == parkId) { keep = true; break; }
    if (keep) {
      newIndex.push_back(parkId);
    } else {
      String fk = selKey('f', parkId), vk = selKey('v', parkId);
      if (_prefs.isKey(fk.c_str())) _prefs.remove(fk.c_str());
      if (_prefs.isKey(vk.c_str())) _prefs.remove(vk.c_str());
    }
  }
  _selectionIndex = newIndex;
  storeSelectionIndex();
  _prefs.end();

  _filterCache.clear();
  _favoriteCache.clear();
  return ok;
}

void ConfigManager::exportRideSelections(JsonObject outFilters,
                                         JsonObject outFavorites) const {
  _prefs.begin(NVS_NAMESPACE, true);
  for (size_t i = 0; i < _selectionIndex.size(); i++) {
    const String& parkId = _selectionIndex[i];
    const char kinds[2] = { 'f', 'v' };
    JsonObject outs[2] = { outFilters, outFavorites };
    for (int k = 0; k < 2; k++) {
      String sel = _prefs.getString(selKey(kinds[k], parkId).c_str(), "");
      if (sel.length() == 0) continue;
      JsonArray arr = outs[k].createNestedArray(parkId);
      for (unsigned p = 0; p + 8 <= sel.length(); p += 8)
        arr.add(sel.substring(p, p + 8));
    }
  }
  _prefs.end();
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

void ConfigManager::saveCustomPalette(uint32_t hdr, uint32_t accent, uint32_t panel) {
  _prefs.begin(NVS_NAMESPACE, false);
  _prefs.putInt("cst_h", (int32_t)hdr);
  _prefs.putInt("cst_a", (int32_t)accent);
  _prefs.putInt("cst_p", (int32_t)panel);
  _prefs.end();
  _config.customHdr    = hdr;
  _config.customAccent = accent;
  _config.customPanel  = panel;
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

void ConfigManager::factoryReset() {
  // WiFiManager stores its credentials in the same namespace, so clearing
  // it wipes those too — that's the point of a factory reset.
  _prefs.begin(NVS_NAMESPACE, false);
  _prefs.clear();     // includes every per-park rf_/fv_ key and sel_idx
  _prefs.end();
  _config = RuntimeConfig();
  _selectionIndex.clear();
  _filterCache.clear();
  _favoriteCache.clear();
}

bool ConfigManager::parseEnabledParks(const String& json,
                                       std::vector<String>& outIds,
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
    const char* id   = obj["id"]   | "";
    const char* name = obj["name"] | "";
    if (strlen(id) > 0) {
      outIds.push_back(String(id));
      outNames.push_back(String(name));
    }
  }
  return outIds.size() > 0;
}

// Lazily fetch (and memoize) one park's selection string from its NVS key.
// The cache stores "" for parks without a key, so each park costs at most
// one NVS read per boot.
const String& ConfigManager::cachedSelection(char kind, const String& parkId,
                                             std::map<String, String>& cache) const {
  auto it = cache.find(parkId);
  if (it != cache.end()) return it->second;
  _prefs.begin(NVS_NAMESPACE, true);
  String sel = _prefs.getString(selKey(kind, parkId).c_str(), "");
  _prefs.end();
  return cache.emplace(parkId, sel).first->second;
}

bool ConfigManager::isRideEnabled(const String& parkId, const String& rideId) const {
  const String& sel = cachedSelection('f', parkId, _filterCache);
  if (sel.length() == 0) return true;   // no filter stored = all rides enabled
  return selectionContains(sel, rideId);
}

// Same storage shape as the ride filter, but the semantics differ:
// absence means "not a favorite", never "all favorites".
bool ConfigManager::isRideFavorite(const String& parkId, const String& rideId) const {
  const String& sel = cachedSelection('v', parkId, _favoriteCache);
  if (sel.length() == 0) return false;
  return selectionContains(sel, rideId);
}
