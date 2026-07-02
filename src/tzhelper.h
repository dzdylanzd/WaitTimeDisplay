#ifndef TZHELPER_H
#define TZHELPER_H

#include <Arduino.h>

void   applyTimeZone(const String& ianaTz);
String getLocalTimeString();
void   resetTimeCache();

// Minutes since local midnight (0–1439). Returns false before the first NTP
// sync — callers should then behave as if outside any quiet-hours window.
bool   getLocalMinutesOfDay(int& outMinutes);

#endif // TZHELPER_H
