#include "queueapi.h"
#include "config.h"
#include "httpjson.h"
#include <WiFi.h>

static const char* TPW_BASE = "https://api.themeparks.wiki/v1";
// Identify ourselves to the API (community-run; polite and helps debugging).
static const char* TPW_UA   = "QueueWatch";

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

String QueueApi::normalizeId(const char* uuid) {
  String out;
  if (!uuid) return out;
  for (const char* p = uuid; *p; p++) {
    char c = *p;
    if (c == '-') continue;
    if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    out += c;
  }
  return out;
}

static RideStatus parseStatus(const char* s) {
  if (!s) return RideStatus::Closed;
  if (strcmp(s, "OPERATING")     == 0) return RideStatus::Operating;
  if (strcmp(s, "DOWN")          == 0) return RideStatus::Down;
  if (strcmp(s, "REFURBISHMENT") == 0) return RideStatus::Refurbishment;
  return RideStatus::Closed;              // CLOSED and anything unknown
}

// Extract minutes-of-day from an ISO8601 timestamp like
// "2026-07-07T15:00:00-04:00" — the offset is the park's own, so the HH:MM
// substring IS park-local wall-clock time. Returns -1 when malformed or the
// date prefix doesn't match wantedDate.
static int localMinutesIfDate(const char* iso, const String& wantedDate) {
  if (!iso || wantedDate.length() != 10) return -1;
  if (strncmp(iso, wantedDate.c_str(), 10) != 0) return -1;
  if (strlen(iso) < 16 || iso[10] != 'T' || iso[13] != ':') return -1;
  int h = (iso[11] - '0') * 10 + (iso[12] - '0');
  int m = (iso[14] - '0') * 10 + (iso[15] - '0');
  if (h < 0 || h > 23 || m < 0 || m > 59) return -1;
  return h * 60 + m;
}

// Find a park's timezone via the tiny /v1/entity/{id} endpoint, caching the
// result. Returns nullptr when the park is unknown or the fetch failed.
const QueueApi::TZCache* QueueApi::lookupPark(const String& parkId) {
  for (int i = 0; i < _tzCacheCount; i++) {
    if (_tzCache[i].parkId == parkId) return &_tzCache[i];
  }

  StaticJsonDocument<128> filter;
  filter["timezone"] = true;
  DynamicJsonDocument doc(512);
  String url = String(TPW_BASE) + "/entity/" + parkId;
  if (!httpGetJson(url, doc, &filter, TPW_UA)) return nullptr;
  const char* tz = doc["timezone"] | "";
  if (strlen(tz) == 0) return nullptr;

  // Even when the cache is full the entry must be returned, so the
  // last slot is overwritten rather than dropping the result.
  int slot = (_tzCacheCount < TZ_CACHE_SIZE) ? _tzCacheCount
                                             : TZ_CACHE_SIZE - 1;
  _tzCache[slot].parkId = parkId;
  _tzCache[slot].tz     = String(tz);
  if (_tzCacheCount < TZ_CACHE_SIZE) _tzCacheCount++;
  return &_tzCache[slot];
}

String QueueApi::getParkTimezone(const String& parkId) {
  const TZCache* e = lookupPark(parkId);
  return e ? e->tz : "UTC";
}

bool QueueApi::getParkHours(const String& parkId, const String& todayDate,
                            ParkHours& out) {
  if (parkId.length() < 8 || todayDate.length() != 10) return false;

  int slot = -1;
  for (int i = 0; i < _hoursCacheCount; i++) {
    if (_hoursCache[i].parkId == parkId) {
      if (_hoursCache[i].hours.date == todayDate) {  // fresh — cache hit
        out = _hoursCache[i].hours;
        return true;
      }
      slot = i;  // stale (yesterday) — refetch into the same slot
      break;
    }
  }

  // The filter drops each day's "purchases" blob (Lightning Lane pricing
  // etc.) — it dominates the ~50 KB raw schedule payload.
  StaticJsonDocument<256> filter;
  JsonObject f = filter["schedule"][0].to<JsonObject>();
  f["date"]        = true;
  f["type"]        = true;
  f["openingTime"] = true;
  f["closingTime"] = true;

  DynamicJsonDocument doc(16384);
  String url = String(TPW_BASE) + "/entity/" + parkId + "/schedule";
  if (!httpGetJson(url, doc, &filter, TPW_UA)) {
    Serial.printf("[api] schedule %s: FAILED (heap %u)\n",
                  parkId.c_str(), (unsigned)ESP.getFreeHeap());
    return false;
  }

  ParkHours h;
  h.date = todayDate;
  for (JsonObject day : doc["schedule"].as<JsonArray>()) {
    if (strcmp(day["type"] | "", "OPERATING") != 0) continue;
    if (todayDate != (day["date"] | "")) continue;

    int open = localMinutesIfDate(day["openingTime"] | "", todayDate);
    if (open < 0) continue;
    // A park closing past midnight has a next-day closingTime — clamp the
    // window to end-of-day so the same-day check in parkClosedNow() holds.
    const char* closeIso = day["closingTime"] | "";
    int close = localMinutesIfDate(closeIso, todayDate);
    if (close < 0) close = (strncmp(closeIso, todayDate.c_str(), 10) > 0) ? 1440 : -1;
    if (close < 0) continue;

    // Multiple OPERATING entries: earliest open, latest close.
    if (h.openMin < 0 || open < h.openMin)   h.openMin  = open;
    if (h.closeMin < 0 || close > h.closeMin) h.closeMin = close;
  }

  if (slot < 0) {
    // Even when the cache is full the result must be cached somewhere, so
    // the last slot is overwritten rather than dropping it (matches TZCache).
    slot = (_hoursCacheCount < HOURS_CACHE_SIZE) ? _hoursCacheCount
                                                 : HOURS_CACHE_SIZE - 1;
    if (_hoursCacheCount < HOURS_CACHE_SIZE) _hoursCacheCount++;
  }
  _hoursCache[slot].parkId = parkId;
  _hoursCache[slot].hours  = h;
  out = h;
  Serial.printf("[api] schedule %s: %s open %d close %d\n", parkId.c_str(),
                todayDate.c_str(), h.openMin, h.closeMin);
  return true;
}

bool QueueApi::fetchRideData(const String& parkId,
                             const String& todayDate, int nowMin,
                             RideInfo rides[], int& rideCount, int maxRides,
                             RideStatus* outParkStatus) {
  if (WiFi.status() != WL_CONNECTED) return false;
  if (parkId.length() < 8) return false;   // not a plausible entity id

  // Per-element filter — only the fields we actually use. Without it, a big
  // entity (paid queues, forecasts) overflows the element document.
  StaticJsonDocument<512> filter;
  filter["id"]         = true;
  filter["name"]       = true;
  filter["entityType"] = true;
  filter["status"]     = true;
  filter["queue"]["STANDBY"]["waitTime"] = true;
  filter["showtimes"][0]["startTime"]    = true;

  // The liveData array is streamed one entity at a time, so memory is
  // bounded by the largest single entity (a show with dozens of showtimes),
  // not the park's size — show-heavy parks like Europa-Park need ~50 KB+
  // for the whole filtered array, which the ESP32 can't afford next to a
  // TLS session.
  DynamicJsonDocument elem(16384);
  String url = String(TPW_BASE) + "/entity/" + parkId + "/live";

  bool ok = httpGetJsonArray(url, "liveData", elem, &filter,
    // A retried attempt restarts the array — drop anything already parsed.
    [&]() { rideCount = 0; },
    [&](JsonObject e) {
      const char* type = e["entityType"] | "";
      if (strcmp(type, "PARK") == 0) {
        if (outParkStatus) *outParkStatus = parseStatus(e["status"] | "");
        return true;
      }
      bool isShow = (strcmp(type, "SHOW") == 0);
      if (!isShow && strcmp(type, "ATTRACTION") != 0) return true;  // RESTAURANT etc.
      if (rideCount >= maxRides) return false;  // stop streaming, keep result

      RideInfo& r = rides[rideCount];
      r.id     = normalizeId(e["id"] | "");
      if (r.id.length() == 0) return true;
      r.name   = sanitizeToAscii(e["name"] | "Unknown");
      r.kind   = isShow ? EntityKind::Show : EntityKind::Attraction;
      r.status = parseStatus(e["status"] | "");
      r.waitTime = -1;
      if (!isShow) {
        JsonVariant wt = e["queue"]["STANDBY"]["waitTime"];
        if (!wt.isNull()) r.waitTime = wt.as<int>();
      }
      // Shows: next remaining showtime today (park-local minutes-of-day).
      r.nextShowMin = -1;
      if (isShow) {
        for (JsonObject st : e["showtimes"].as<JsonArray>()) {
          int min = localMinutesIfDate(st["startTime"] | "", todayDate);
          if (min >= nowMin && (r.nextShowMin < 0 || min < r.nextShowMin))
            r.nextShowMin = (int16_t)min;
        }
      }
      r.trend      = 0;
      r.trendDelta = 0;
      r.favorite   = false;
      rideCount++;
      return true;
    },
    TPW_UA);

  Serial.printf("[api] live %s: %s, %d entities (heap %u)\n",
                parkId.c_str(), ok ? "ok" : "FAILED", rideCount,
                (unsigned)ESP.getFreeHeap());
  return ok && rideCount > 0;
}

bool QueueApi::fetchAvailableParks(std::vector<String>& outIds,
                                   std::vector<String>& outNames,
                                   std::vector<String>& outGroups) {
  outIds.clear();
  outNames.clear();
  outGroups.clear();

  StaticJsonDocument<256> filter;
  JsonObject fDest = filter["destinations"][0].to<JsonObject>();
  fDest["name"] = true;
  JsonObject fPark = fDest["parks"][0].to<JsonObject>();
  fPark["id"] = true; fPark["name"] = true;

  DynamicJsonDocument doc(32768);
  String url = String(TPW_BASE) + "/destinations";
  if (!httpGetJson(url, doc, &filter, TPW_UA)) return false;

  for (JsonObject dest : doc["destinations"].as<JsonArray>()) {
    const char* groupName = dest["name"] | "Other";
    for (JsonObject park : dest["parks"].as<JsonArray>()) {
      const char* id   = park["id"]   | "";
      const char* name = park["name"] | "";
      if (strlen(id) > 0 && strlen(name) > 0) {
        outIds.push_back(String(id));                // dashed — used in URLs
        outNames.push_back(sanitizeToAscii(name));   // shown on LCD → ASCII only
        outGroups.push_back(String(groupName));      // browser only → keep UTF-8
      }
    }
  }

  return outIds.size() > 0;
}
