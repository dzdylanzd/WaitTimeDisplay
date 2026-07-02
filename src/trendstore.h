#ifndef TRENDSTORE_H
#define TRENDSTORE_H

#include <stdint.h>

// Remembers the last observed wait time per ride (keyed by queue-times ride
// id) across API refreshes and park switches, so the display can show a
// rising/falling arrow. RAM-only: history resets on reboot, which is fine —
// a trend only means something within a session anyway.
class TrendStore {
public:
  // Threshold below which a change counts as noise, not a trend.
  static constexpr int TREND_THRESHOLD_MIN = 5;

  // Record newWait for rideId and return the delta vs. the previous
  // observation. Returns 0 (flat) when: first sighting, the ride is closed
  // (newWait < 0 — closed observations neither update nor report), or the
  // change is smaller than TREND_THRESHOLD_MIN. The stored value is always
  // advanced on an open observation, so a slow drift eventually registers.
  int updateAndGetDelta(int rideId, int newWait);

  void clear();

private:
  struct Entry {
    int32_t rideId   = -1;
    int16_t lastWait = -1;
  };
  static constexpr int CAPACITY = 250;   // ~2 KB — covers several large parks
  Entry _entries[CAPACITY];
  int   _nextEvict = 0;                  // round-robin slot reuse when full
  int   _count     = 0;
};

#endif // TRENDSTORE_H
