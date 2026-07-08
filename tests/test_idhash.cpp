#include "doctest.h"
#include "../src/idhash.h"

TEST_CASE("fnv1a32: stable known values") {
    // FNV-1a reference values — must never change (TrendStore keys and any
    // future persisted hashes depend on the exact function).
    CHECK(fnv1a32("") == 2166136261u);
    CHECK(fnv1a32("a") == 0xE40C292Cu);
    CHECK(fnv1a32("foobar") == 0xBF9CF968u);
}

TEST_CASE("fnv1a32: distinct UUIDs hash differently") {
    uint32_t a = fnv1a32("aaaaaaaa111122223333444444444444");
    uint32_t b = fnv1a32("aaaaaaaa111122223333444444444445");
    uint32_t c = fnv1a32("bbbbbbbb111122223333444444444444");
    CHECK(a != b);
    CHECK(a != c);
    CHECK(b != c);
    // And repeatable
    CHECK(a == fnv1a32("aaaaaaaa111122223333444444444444"));
}
