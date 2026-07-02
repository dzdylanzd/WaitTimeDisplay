// Simulator implementation: returns the host machine's current local time.
#include "../src/tzhelper.h"
#include <ctime>
#include <cstdio>

void applyTimeZone(const String&) {}
void resetTimeCache() {}

String getLocalTimeString() {
    time_t t = time(nullptr);
    const struct tm* tm_info = localtime(&t);
    char buf[6];
    strftime(buf, sizeof(buf), "%H:%M", tm_info);
    return String(buf);
}

bool getLocalMinutesOfDay(int& outMinutes) {
    time_t t = time(nullptr);
    const struct tm* tm_info = localtime(&t);
    outMinutes = tm_info->tm_hour * 60 + tm_info->tm_min;
    return true;
}
