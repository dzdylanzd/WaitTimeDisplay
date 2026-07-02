#include "queueapi.h"
#include "config.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

#if __has_include(<esp_task_wdt.h>)
#include <esp_task_wdt.h>
#define FEED_WDT() esp_task_wdt_reset()
#else
#define FEED_WDT() ((void)0)
#endif

QueueApi::QueueApi() {
  for (int i = 0; i < TZ_CACHE_SIZE; i++) {
    _tzCache[i].parkId = -1;
  }
}

// Map a Unicode code point to its closest ASCII form, or "" to drop it.
// The LCD's Montserrat font only contains ASCII glyphs, so anything else
// (accents, smart quotes, TM/(R), dashes) would render as a blank box.
static const char* asciiFold(uint32_t cp) {
  switch (cp) {
    case 0xC0: case 0xC1: case 0xC2: case 0xC3: case 0xC4: case 0xC5: return "A";
    case 0xE0: case 0xE1: case 0xE2: case 0xE3: case 0xE4: case 0xE5: return "a";
    case 0xC6: return "AE"; case 0xE6: return "ae";
    case 0xC7: return "C";  case 0xE7: return "c";
    case 0xC8: case 0xC9: case 0xCA: case 0xCB: return "E";
    case 0xE8: case 0xE9: case 0xEA: case 0xEB: return "e";
    case 0xCC: case 0xCD: case 0xCE: case 0xCF: return "I";
    case 0xEC: case 0xED: case 0xEE: case 0xEF: return "i";
    case 0xD1: return "N";  case 0xF1: return "n";
    case 0xD2: case 0xD3: case 0xD4: case 0xD5: case 0xD6: case 0xD8: return "O";
    case 0xF2: case 0xF3: case 0xF4: case 0xF5: case 0xF6: case 0xF8: return "o";
    case 0xD9: case 0xDA: case 0xDB: case 0xDC: return "U";
    case 0xF9: case 0xFA: case 0xFB: case 0xFC: return "u";
    case 0xDD: return "Y";  case 0xFD: case 0xFF: return "y";
    case 0xDF: return "ss";                  // ß
    case 0x2018: case 0x2019: return "'";     // ‘ ’
    case 0x201C: case 0x201D: return "\"";    // “ ”
    case 0x2013: case 0x2014: return "-";     // – —
    case 0x2026: return "...";                // …
    case 0x00B7: case 0x2022: return "-";     // · •
    case 0x2122: case 0x00AE: case 0x00A9: return "";  // ™ ® ©
    default: return nullptr;                  // unknown non-ASCII — drop
  }
}

// Transliterate UTF-8 text to the ASCII subset the LCD font can render.
static String sanitizeToAscii(const char* src) {
  String out;
  if (!src) return out;
  const unsigned char* p = (const unsigned char*)src;
  while (*p) {
    unsigned char b = *p;
    if (b < 0x80) { out += (char)b; p++; continue; }

    uint32_t cp = 0; int cont = 0;
    if      ((b & 0xE0) == 0xC0) { cp = b & 0x1F; cont = 1; }
    else if ((b & 0xF0) == 0xE0) { cp = b & 0x0F; cont = 2; }
    else if ((b & 0xF8) == 0xF0) { cp = b & 0x07; cont = 3; }
    else { p++; continue; }                   // invalid lead byte
    p++;
    for (int i = 0; i < cont && (*p & 0xC0) == 0x80; i++, p++)
      cp = (cp << 6) | (*p & 0x3F);

    const char* rep = asciiFold(cp);
    if (rep) out += rep;
  }
  return out;
}

bool QueueApi::httpGetJson(const String& url, DynamicJsonDocument& doc,
                           JsonDocument* filter) {
  // On device, WiFi must be connected before making any HTTP call.
  // In the sim (SIMULATION=1) we skip this check — internet is always available.
#ifndef SIMULATION
  if (WiFi.status() != WL_CONNECTED) return false;
#endif

  for (uint8_t attempt = 0; attempt <= HTTP_RETRY_MAX; attempt++) {
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;

    if (!http.begin(client, url)) {
      http.end();
      return false;
    }
    http.setTimeout(HTTP_TIMEOUT_MS);

    FEED_WDT();
    int httpCode = http.GET();
    FEED_WDT();

    if (httpCode == 200) {
      int size = http.getSize();
      if (size > (int)HTTP_MAX_RESPONSE_SIZE) {
        Serial.printf("HTTP response too large: %d bytes (max %u)\n",
                      size, (unsigned)HTTP_MAX_RESPONSE_SIZE);
        http.end();
        return false;
      }

      String payload = http.getString();
      http.end();

      FEED_WDT();
      doc.clear();
      DeserializationError error = filter
        ? deserializeJson(doc, payload, DeserializationOption::Filter(*filter))
        : deserializeJson(doc, payload);
      if (error) {
        Serial.printf("JSON parse error: %s\n", error.c_str());
        return false;
      }
      return true;
    }

    Serial.printf("HTTP %s attempt %u failed: %d\n",
                  url.c_str(), attempt, httpCode);
    http.end();
    delay(300);
  }

  return false;
}

String QueueApi::getParkTimezone(int parkId) {
  for (int i = 0; i < _tzCacheCount; i++) {
    if (_tzCache[i].parkId == parkId) return _tzCache[i].tz;
  }

  DynamicJsonDocument doc(65536);
  if (!httpGetJson("https://queue-times.com/parks.json", doc)) return "UTC";

  JsonArray groups = doc.as<JsonArray>();
  for (JsonObject group : groups) {
    JsonArray parks = group["parks"].as<JsonArray>();
    for (JsonObject park : parks) {
      int id = park["id"] | -1;
      if (id == parkId) {
        String tz(park["timezone"] | "UTC");
        if (_tzCacheCount < TZ_CACHE_SIZE) {
          _tzCache[_tzCacheCount].parkId = parkId;
          _tzCache[_tzCacheCount].tz = tz;
          _tzCacheCount++;
        }
        return tz;
      }
    }
  }

  return "UTC";
}

void QueueApi::appendRide(JsonObject ride, const char* landName,
                          RideInfo rides[], int& rideCount) {
  rides[rideCount].id       = ride["id"] | -1;
  rides[rideCount].name     = sanitizeToAscii(ride["name"] | "Unknown");
  rides[rideCount].land     = sanitizeToAscii(landName);
  rides[rideCount].waitTime = ride["wait_time"] | -1;
  rides[rideCount].isOpen   = ride["is_open"] | false;
  rides[rideCount].trend      = 0;
  rides[rideCount].trendDelta = 0;
  rides[rideCount].favorite   = false;
  rideCount++;
}

bool QueueApi::fetchRideData(int parkId,
                              RideInfo rides[], int& rideCount,
                              int maxRides) {
  if (WiFi.status() != WL_CONNECTED) return false;
  if (parkId <= 0) return false;

  // Only parse the fields we actually use. Without a filter, large parks
  // (e.g. Canada's Wonderland) overflow the document and fail with NoMemory.
  StaticJsonDocument<512> filter;
  filter["lands"][0]["name"] = true;   // land name shown on the ride screen
  JsonObject fLand = filter["lands"][0]["rides"][0].to<JsonObject>();
  fLand["id"] = true; fLand["name"] = true;
  fLand["wait_time"] = true; fLand["is_open"] = true;
  JsonObject fTop = filter["rides"][0].to<JsonObject>();
  fTop["id"] = true; fTop["name"] = true;
  fTop["wait_time"] = true; fTop["is_open"] = true;

  DynamicJsonDocument doc(32768);
  String url = String("https://queue-times.com/parks/") +
               parkId + "/queue_times.json";
  if (!httpGetJson(url, doc, &filter)) return false;

  rideCount = 0;
  JsonArray lands = doc["lands"].as<JsonArray>();

  for (JsonObject land : lands) {
    const char* landName = land["name"] | "";
    JsonArray ridesArray = land["rides"].as<JsonArray>();
    for (JsonObject ride : ridesArray) {
      if (rideCount >= maxRides) break;
      appendRide(ride, landName, rides, rideCount);
    }
  }

  // Some parks (e.g. Tokyo Disneyland/DisneySea) return no lands and put
  // all rides in a top-level "rides" array instead.
  JsonArray topRides = doc["rides"].as<JsonArray>();
  for (JsonObject ride : topRides) {
    if (rideCount >= maxRides) break;
    appendRide(ride, "", rides, rideCount);
  }

  return rideCount > 0;
}

bool QueueApi::fetchAvailableParks(std::vector<int>& outIds,
                                    std::vector<String>& outNames,
                                    std::vector<String>& outGroups) {
  outIds.clear();
  outNames.clear();
  outGroups.clear();

  DynamicJsonDocument doc(65536);
  if (!httpGetJson("https://queue-times.com/parks.json", doc)) return false;

  JsonArray groups = doc.as<JsonArray>();
  for (JsonObject group : groups) {
    const char* groupName = group["name"] | "Other";
    JsonArray parks = group["parks"].as<JsonArray>();
    for (JsonObject park : parks) {
      int id = park["id"] | -1;
      const char* name = park["name"] | "";
      if (id > 0 && strlen(name) > 0) {
        outIds.push_back(id);
        outNames.push_back(sanitizeToAscii(name));   // shown on LCD → ASCII only
        outGroups.push_back(String(groupName));      // browser only → keep UTF-8
      }
    }
  }

  return outIds.size() > 0;
}
