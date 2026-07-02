#ifndef TZHELPER_H
#define TZHELPER_H

#include <Arduino.h>

void   applyTimeZone(const String& ianaTz);
String getLocalTimeString();
void   resetTimeCache();

// Minutes since local midnight (0–1439). Returns false before the first NTP
// sync — callers should then behave as if outside any quiet-hours window.
bool   getLocalMinutesOfDay(int& outMinutes);

// Same, but in a specific IANA timezone (e.g. the user's home zone) instead
// of the currently applied park timezone. Returns false before NTP sync or
// when the zone isn't in the mapping table.
bool   getMinutesOfDayInTz(const String& ianaTz, int& outMinutes);

#endif // TZHELPER_H
