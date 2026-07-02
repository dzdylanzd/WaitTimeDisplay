#include "ridefilter.h"
#include <algorithm>

// Sort rank within the wait-desc mode: open rides by wait descending,
// unknown-wait open rides after them, closed rides last.
static int waitRank(const RideInfo& r) {
  if (!r.isOpen)        return -2000;
  if (r.waitTime < 0)   return -1000;
  return r.waitTime;
}

void applyDisplayOptions(RideInfo rides[], int& count,
                         const RideDisplayOptions& opt) {
  if (count <= 0) return;

  // ---- Filter (compact in place, with revert-if-empty safety) ----
  if (opt.skipClosed || opt.minWaitMinutes > 0) {
    int writeIdx = 0;
    for (int i = 0; i < count; i++) {
      const RideInfo& r = rides[i];
      if (opt.skipClosed && !r.isOpen) continue;
      if (opt.minWaitMinutes > 0 && r.isOpen &&
          r.waitTime >= 0 && r.waitTime < (int)opt.minWaitMinutes) continue;
      if (writeIdx != i) rides[writeIdx] = rides[i];
      writeIdx++;
    }
    // Empty result would show a confusing NO_RIDES error (and break the
    // closed-park screen); an unfiltered list is more useful than none.
    if (writeIdx > 0) count = writeIdx;
  }

  // ---- Sort (stable, so API order is the tiebreaker) ----
  bool sortByWait = (opt.sortMode == SORT_MODE_WAIT_DESC);
  if (!opt.favoritesFirst && !sortByWait) return;

  std::stable_sort(rides, rides + count,
    [&](const RideInfo& a, const RideInfo& b) {
      if (opt.favoritesFirst && a.favorite != b.favorite) return a.favorite;
      if (sortByWait) return waitRank(a) > waitRank(b);
      return false;
    });
}
