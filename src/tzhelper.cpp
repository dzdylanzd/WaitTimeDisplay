#include "tzhelper.h"
#include "tzposix.h"   // shared IANA→POSIX table (also used by the sim)
#include <time.h>

static String _lastValidTime;

void resetTimeCache() {
  _lastValidTime = String();
}

void applyTimeZone(const String& tz) {
  const char* posix = lookupPosixTZ(tz);
  if (posix) configTzTime(posix, "pool.ntp.org", "time.nist.gov");
  else        configTzTime("UTC0", "pool.ntp.org", "time.nist.gov");
  resetTimeCache();
  delay(200);
}

String getLocalTimeString() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return _lastValidTime.length() > 0 ? _lastValidTime : "--:--";
  }
  char buf[6];
  snprintf(buf, sizeof(buf), "%02d:%02d",
           timeinfo.tm_hour, timeinfo.tm_min);
  _lastValidTime = String(buf);
  return _lastValidTime;
}

bool getLocalMinutesOfDay(int& outMinutes) {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return false;
  outMinutes = timeinfo.tm_hour * 60 + timeinfo.tm_min;
  return true;
}

String getLocalDateString() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return String();
  char buf[11];
  snprintf(buf, sizeof(buf), "%04d-%02d-%02d",
           timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday);
  return String(buf);
}

// Convert the current UTC epoch into another zone by swapping the TZ env var
// around a localtime_r call. System time itself is zone-less, so this doesn't
// disturb NTP or the displayed park clock; everything runs on the loop() task,
// so nothing can observe the temporary TZ.
bool getMinutesOfDayInTz(const String& ianaTz, int& outMinutes) {
  const char* posix = lookupPosixTZ(ianaTz);
  if (!posix) return false;

  time_t now = time(nullptr);
  if (now < 1600000000) return false;   // clock not NTP-synced yet (< 2020)

  const char* oldTz = getenv("TZ");
  String saved = oldTz ? oldTz : "";

  setenv("TZ", posix, 1);
  tzset();
  struct tm timeinfo;
  localtime_r(&now, &timeinfo);

  if (saved.length() > 0) setenv("TZ", saved.c_str(), 1);
  else                    unsetenv("TZ");
  tzset();

  outMinutes = timeinfo.tm_hour * 60 + timeinfo.tm_min;
  return true;
}
