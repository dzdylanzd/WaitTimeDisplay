#ifndef TZHELPER_H
#define TZHELPER_H

#include <Arduino.h>

void   applyTimeZone(const String& ianaTz);
String getLocalTimeString();
void   resetTimeCache();

#endif // TZHELPER_H
