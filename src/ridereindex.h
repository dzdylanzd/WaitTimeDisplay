#ifndef RIDEREINDEX_H
#define RIDEREINDEX_H

#include "ridefilter.h"

// Re-find, by ride id, the index the user was looking at before a refresh
// that may have resorted the list (e.g. wait-desc mode reorders on every
// fetch). Falls back to the old index if it's still in range, else 0.
// Pure function, no class state, independently testable.
inline int reindexAfterRefresh(const String prevIds[], int prevCount, int prevIndex,
                                const RideInfo newRides[], int newCount) {
  if (newCount == 0) return 0;
  String currentRideId =
      (prevCount > 0 && prevIndex < prevCount) ? prevIds[prevIndex] : String();
  if (currentRideId.length() > 0) {
    for (int i = 0; i < newCount; i++) {
      if (newRides[i].id == currentRideId) return i;
    }
  }
  return (prevIndex < newCount) ? prevIndex : 0;
}

#endif // RIDEREINDEX_H
