#ifndef TZPOSIX_H
#define TZPOSIX_H

#include <Arduino.h>

// IANA → POSIX timezone mapping, shared by the device implementation
// (src/tzhelper.cpp) and the simulator's (sim/tzhelper_sim.cpp), so the sim
// shows real park-local time instead of the host clock.
//
// Keep the entry list (and their count) in sync with TZ_LIST in the web UI
// (src/cfgserver.cpp), which is the dropdown users pick "deviceTimezone"
// from. The list must stay sorted case-insensitively by iana for
// lookupPosixTZ's binary search below.

struct TZEntry { const char* iana; const char* posix; };

static const TZEntry TZ_TABLE[] = {
  {"America/Chicago",       "CST6CDT,M3.2.0/2,M11.1.0/2"},
  {"America/Denver",        "MST7MDT,M3.2.0/2,M11.1.0/2"},
  {"America/Detroit",       "EST5EDT,M3.2.0/2,M11.1.0/2"},
  {"America/Halifax",       "AST4ADT,M3.2.0/2,M11.1.0/2"},
  {"America/Los_Angeles",   "PST8PDT,M3.2.0/2,M11.1.0/2"},
  {"America/Mexico_City",   "CST6CDT,M4.1.0/2,M10.5.0/2"},
  {"America/New_York",      "EST5EDT,M3.2.0/2,M11.1.0/2"},
  {"America/Phoenix",       "MST7"},
  {"America/Sao_Paulo",     "BRT3"},
  {"America/Toronto",       "EST5EDT,M3.2.0/2,M11.1.0/2"},
  {"America/Vancouver",     "PST8PDT,M3.2.0/2,M11.1.0/2"},
  {"Asia/Bangkok",          "ICT-7"},
  {"Asia/Beijing",          "CST-8"},
  {"Asia/Dubai",            "GST-4"},
  {"Asia/Hong_Kong",        "HKT-8"},
  {"Asia/Istanbul",         "TRT-3"},
  {"Asia/Jakarta",          "WIB-7"},
  {"Asia/Kolkata",          "IST-5:30"},
  {"Asia/Kuala_Lumpur",     "MYT-8"},
  {"Asia/Macau",            "CST-8"},
  {"Asia/Muscat",           "GST-4"},
  {"Asia/Riyadh",           "AST-3"},
  {"Asia/Seoul",            "KST-9"},
  {"Asia/Shanghai",         "CST-8"},
  {"Asia/Singapore",        "SGT-8"},
  {"Asia/Taipei",           "CST-8"},
  {"Asia/Tokyo",            "JST-9"},
  {"Australia/Brisbane",    "AEST-10"},
  {"Australia/Melbourne",   "AEST-10AEDT,M10.1.0/2,M4.1.0/3"},
  {"Australia/Perth",       "AWST-8"},
  {"Australia/Sydney",      "AEST-10AEDT,M10.1.0/2,M4.1.0/3"},
  {"Europe/Amsterdam",      "CET-1CEST,M3.5.0/2,M10.5.0/3"},
  {"Europe/Berlin",         "CET-1CEST,M3.5.0/2,M10.5.0/3"},
  {"Europe/Brussels",       "CET-1CEST,M3.5.0/2,M10.5.0/3"},
  {"Europe/Budapest",       "CET-1CEST,M3.5.0/2,M10.5.0/3"},
  {"Europe/Copenhagen",     "CET-1CEST,M3.5.0/2,M10.5.0/3"},
  {"Europe/Dublin",         "GMT0BST,M3.5.0/1,M10.5.0/2"},
  {"Europe/Helsinki",       "EET-2EEST,M3.5.0/3,M10.5.0/4"},
  {"Europe/Lisbon",         "GMT0BST,M3.5.0/1,M10.5.0/2"},
  {"Europe/London",         "GMT0BST,M3.5.0/1,M10.5.0/2"},
  {"Europe/Madrid",         "CET-1CEST,M3.5.0/2,M10.5.0/3"},
  {"Europe/Oslo",           "CET-1CEST,M3.5.0/2,M10.5.0/3"},
  {"Europe/Paris",          "CET-1CEST,M3.5.0/2,M10.5.0/3"},
  {"Europe/Prague",         "CET-1CEST,M3.5.0/2,M10.5.0/3"},
  {"Europe/Rome",           "CET-1CEST,M3.5.0/2,M10.5.0/3"},
  {"Europe/Stockholm",      "CET-1CEST,M3.5.0/2,M10.5.0/3"},
  {"Europe/Vienna",         "CET-1CEST,M3.5.0/2,M10.5.0/3"},
  {"Europe/Warsaw",         "CET-1CEST,M3.5.0/2,M10.5.0/3"},
  {"Europe/Zurich",         "CET-1CEST,M3.5.0/2,M10.5.0/3"},
  {"Pacific/Auckland",      "NZST-12NZDT,M9.5.0/2,M4.1.0/3"},
  {"Pacific/Guam",          "ChST-10"},
  {"Pacific/Honolulu",      "HST10"},
};

static const int TZ_TABLE_SIZE = sizeof(TZ_TABLE) / sizeof(TZ_TABLE[0]);

static inline int tzCiCompare(const char* a, const char* b) {
  while (*a && *b) {
    char ca = *a, cb = *b;
    if (ca >= 'A' && ca <= 'Z') ca += 32;
    if (cb >= 'A' && cb <= 'Z') cb += 32;
    if (ca != cb) return (ca < cb) ? -1 : 1;
    a++; b++;
  }
  if (*a == *b) return 0;
  return (*a < *b) ? -1 : 1;
}

static inline const char* lookupPosixTZ(const String& ianaTz) {
  int lo = 0, hi = TZ_TABLE_SIZE - 1;
  while (lo <= hi) {
    int mid = lo + (hi - lo) / 2;
    int cmp = tzCiCompare(TZ_TABLE[mid].iana, ianaTz.c_str());
    if (cmp == 0) return TZ_TABLE[mid].posix;
    else if (cmp < 0) lo = mid + 1;
    else hi = mid - 1;
  }
  return nullptr;
}

#endif // TZPOSIX_H
