#ifndef VERSIONCOMPARE_H
#define VERSIONCOMPARE_H

#include <Arduino.h>

// Parse a dotted numeric version ("1.2.3", "v1.2", "1.2.3.4", "1.2.0-rc1")
// into up to 4 numeric components. A leading 'v'/'V' is ignored; parsing
// stops at the first non-numeric, non-dot character (so a "-rc1" suffix is
// dropped, i.e. "1.2.0-rc1" parses as 1.2.0). Missing trailing components
// default to 0. Returns false only when there's no leading numeric component
// at all (e.g. "" or "nightly").
inline bool parseVersion(const String& s, long out[4]) {
  out[0] = out[1] = out[2] = out[3] = 0;
  const char* p = s.c_str();
  if (*p == 'v' || *p == 'V') p++;
  int i = 0;
  bool any = false;
  while (*p && i < 4) {
    if (*p < '0' || *p > '9') break;
    long v = 0;
    while (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); p++; }
    out[i++] = v;
    any = true;
    if (*p == '.') p++;
    else break;
  }
  return any;
}

// True when `latest` is a strictly newer semantic version than `current`.
// If either string can't be parsed as a numeric version, falls back to a
// plain inequality test (ignoring a leading 'v') so a non-standard tag still
// surfaces an update rather than silently hiding one — the OTA check would
// rather over-offer than miss a real release.
inline bool isNewerVersion(const String& current, const String& latest) {
  long c[4], l[4];
  bool okc = parseVersion(current, c);
  bool okl = parseVersion(latest, l);
  if (!okc || !okl) {
    String a = current, b = latest;
    if (a.length() && (a.charAt(0) == 'v' || a.charAt(0) == 'V')) a = a.substring(1);
    if (b.length() && (b.charAt(0) == 'v' || b.charAt(0) == 'V')) b = b.substring(1);
    return a != b;
  }
  for (int i = 0; i < 4; i++) {
    if (l[i] > c[i]) return true;
    if (l[i] < c[i]) return false;
  }
  return false;   // identical
}

#endif // VERSIONCOMPARE_H
