#pragma once
// src/tzhelper.cpp is never compiled on Windows otherwise (the sim swaps in
// tzhelper_sim.cpp instead), so it has never needed to satisfy MSVC's CRT.
// It calls a handful of POSIX/ESP-IDF functions with no MSVC equivalent:
// configTzTime/getLocalTime (ESP32 Arduino core), setenv/unsetenv/tzset and
// localtime_r (POSIX). Provide MSVC-only stand-ins so test_tzhelper.cpp can
// #include the real tzhelper.cpp unmodified and test its pure logic
// (TZ_TABLE sortedness, lookupPosixTZ) without touching production code.
#include <time.h>
#include <stdlib.h>
#include <stdint.h>

inline void configTzTime(const char*, const char*, const char*) {}

inline bool getLocalTime(struct tm* info, uint32_t /*timeoutMs*/ = 5000) {
    time_t t = time(nullptr);
    return localtime_s(info, &t) == 0;
}

inline int setenv(const char* name, const char* value, int) {
    return _putenv_s(name, value);
}
inline int unsetenv(const char* name) {
    return _putenv_s(name, "");
}
#define tzset _tzset

inline struct tm* localtime_r(const time_t* t, struct tm* result) {
    return (localtime_s(result, t) == 0) ? result : nullptr;
}
