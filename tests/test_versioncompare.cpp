#include "doctest.h"
#include "../src/versioncompare.h"

TEST_CASE("parseVersion: standard three-part version") {
    long v[4];
    CHECK(parseVersion("1.2.3", v));
    CHECK(v[0] == 1); CHECK(v[1] == 2); CHECK(v[2] == 3); CHECK(v[3] == 0);
}

TEST_CASE("parseVersion: leading v is ignored") {
    long v[4];
    CHECK(parseVersion("v1.2.3", v));
    CHECK(v[0] == 1); CHECK(v[1] == 2); CHECK(v[2] == 3);
    CHECK(parseVersion("V2.0.0", v));
    CHECK(v[0] == 2);
}

TEST_CASE("parseVersion: missing trailing parts default to zero") {
    long v[4];
    CHECK(parseVersion("1.2", v));
    CHECK(v[0] == 1); CHECK(v[1] == 2); CHECK(v[2] == 0); CHECK(v[3] == 0);
    CHECK(parseVersion("3", v));
    CHECK(v[0] == 3); CHECK(v[1] == 0);
}

TEST_CASE("parseVersion: four-part version and multi-digit components") {
    long v[4];
    CHECK(parseVersion("1.10.0.5", v));
    CHECK(v[0] == 1); CHECK(v[1] == 10); CHECK(v[2] == 0); CHECK(v[3] == 5);
}

TEST_CASE("parseVersion: pre-release suffix is dropped at the first non-numeric") {
    long v[4];
    CHECK(parseVersion("1.2.0-rc1", v));
    CHECK(v[0] == 1); CHECK(v[1] == 2); CHECK(v[2] == 0);
}

TEST_CASE("parseVersion: non-numeric strings fail") {
    long v[4];
    CHECK_FALSE(parseVersion("", v));
    CHECK_FALSE(parseVersion("nightly", v));
    CHECK_FALSE(parseVersion("v", v));
}

TEST_CASE("isNewerVersion: strictly newer in each component") {
    CHECK(isNewerVersion("1.2.0", "1.2.1"));
    CHECK(isNewerVersion("1.2.0", "1.3.0"));
    CHECK(isNewerVersion("1.2.0", "2.0.0"));
    CHECK(isNewerVersion("1.9.0", "1.10.0"));   // numeric, not lexical
}

TEST_CASE("isNewerVersion: equal versions are not newer") {
    CHECK_FALSE(isNewerVersion("1.2.0", "1.2.0"));
    CHECK_FALSE(isNewerVersion("1.2.0", "v1.2.0"));   // v-prefix mismatch only
    CHECK_FALSE(isNewerVersion("1.2.0", "1.2"));      // 1.2 == 1.2.0
}

TEST_CASE("isNewerVersion: older latest is rejected (no downgrade offers)") {
    CHECK_FALSE(isNewerVersion("1.2.0", "1.1.9"));
    CHECK_FALSE(isNewerVersion("1.10.0", "1.9.0"));   // the lexical-trap case
    CHECK_FALSE(isNewerVersion("2.0.0", "1.9.9"));
}

TEST_CASE("isNewerVersion: unparseable tags fall back to string inequality") {
    // Neither side parses as a version -> any difference counts as newer, so
    // a non-standard tag still surfaces an update instead of hiding one.
    CHECK(isNewerVersion("nightly", "nightly-2"));
    CHECK_FALSE(isNewerVersion("nightly", "nightly"));
    // One side unparseable also falls back to string compare.
    CHECK(isNewerVersion("stable", "1.0.0"));
}
