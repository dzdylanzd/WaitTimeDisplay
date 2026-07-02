#include "trendstore.h"

int TrendStore::updateAndGetDelta(int rideId, int newWait) {
  if (rideId < 0) return 0;
  if (newWait < 0) return 0;   // closed / no data: keep the last open value

  for (int i = 0; i < _count; i++) {
    if (_entries[i].rideId == rideId) {
      int delta = newWait - _entries[i].lastWait;
      _entries[i].lastWait = (int16_t)newWait;
      return (delta >= TREND_THRESHOLD_MIN || delta <= -TREND_THRESHOLD_MIN)
               ? delta : 0;
    }
  }

  // First sighting — remember it, report flat
  int slot;
  if (_count < CAPACITY) {
    slot = _count++;
  } else {
    slot = _nextEvict;
    _nextEvict = (_nextEvict + 1) % CAPACITY;
  }
  _entries[slot].rideId   = rideId;
  _entries[slot].lastWait = (int16_t)newWait;
  return 0;
}

void TrendStore::clear() {
  for (int i = 0; i < _count; i++) _entries[i] = Entry();
  _count = 0;
  _nextEvict = 0;
}
