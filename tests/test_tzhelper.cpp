#include "doctest.h"
// MSVC-only shims for the POSIX/ESP-IDF functions tzhelper.cpp calls (see the
// header for why); must come before the #include below.
#include "tzhelper_msvc_shims.h"
// Pulls in tzhelper.cpp's static (internal-linkage) TZ_TABLE/lookupPosixTZ
// directly. Do NOT also add src/tzhelper.cpp to this target's source list
// in CMakeLists.txt -- that would duplicate every symbol defined here.
#include "../src/tzhelper.cpp"
#include <cstring>

static int ciCompareTest(const char* a, const char* b) {
#ifdef _WIN32
    return _stricmp(a, b);
#else
    return strcasecmp(a, b);
#endif
}

TEST_CASE("TZ_TABLE is sorted case-insensitively by iana (binary search precondition)") {
    for (int i = 1; i < TZ_TABLE_SIZE; i++) {
        INFO("comparing '", TZ_TABLE[i-1].iana, "' and '", TZ_TABLE[i].iana, "'");
        CHECK(ciCompareTest(TZ_TABLE[i-1].iana, TZ_TABLE[i].iana) < 0);
    }
}

TEST_CASE("lookupPosixTZ finds every table entry by its own iana name") {
    for (int i = 0; i < TZ_TABLE_SIZE; i++) {
        const char* posix = lookupPosixTZ(String(TZ_TABLE[i].iana));
        REQUIRE(posix != nullptr);
        CHECK(String(posix) == String(TZ_TABLE[i].posix));
    }
}

TEST_CASE("lookupPosixTZ is case-insensitive and rejects unknown zones") {
    CHECK(lookupPosixTZ(String("america/new_york")) != nullptr);
    CHECK(lookupPosixTZ(String("AMERICA/NEW_YORK")) != nullptr);
    CHECK(lookupPosixTZ(String("Mars/Olympus_Mons")) == nullptr);
    CHECK(lookupPosixTZ(String("")) == nullptr);
}

TEST_CASE("getMinutesOfDayInTz: known zone succeeds, unmapped zone fails") {
    int minutes = -1;
    bool ok = getMinutesOfDayInTz(String("America/New_York"), minutes);
    // The test host's wall clock is expected to be NTP-synced/current, so
    // this should succeed and produce a value in [0, 1439].
    CHECK(ok);
    if (ok) {
        CHECK(minutes >= 0);
        CHECK(minutes < 1440);
    }

    int unusedMinutes = -1;
    CHECK_FALSE(getMinutesOfDayInTz(String("Not/AZone"), unusedMinutes));
}
