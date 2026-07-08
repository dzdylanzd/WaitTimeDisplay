#ifndef IDHASH_H
#define IDHASH_H

#include <stdint.h>

// FNV-1a 32-bit hash. themeparks.wiki entity ids are UUID strings, but
// TrendStore keys its fixed 250-entry table by int32 — hashing keeps that
// table at ~2 KB instead of holding 250 heap Strings. Collision odds at a
// few hundred live entries are negligible, and the worst case is a wrong
// trend arrow for one ride.
inline uint32_t fnv1a32(const char* s) {
  uint32_t h = 2166136261u;
  while (*s) {
    h ^= (uint8_t)*s++;
    h *= 16777619u;
  }
  return h;
}

#endif // IDHASH_H
