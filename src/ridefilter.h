#ifndef RIDEFILTER_H
#define RIDEFILTER_H

#include "queueapi.h"
#include "configmanager.h"

struct RideDisplayOptions {
  uint8_t sortMode        = SORT_MODE_API_ORDER;
  bool    favoritesFirst  = true;
  bool    skipClosed      = false;
  uint8_t minWaitMinutes  = 0;
};

// In-place filter + sort of rides[0..count), applied AFTER the enabled-ride
// filter and favorite/trend annotation:
//   - drop closed rides when skipClosed
//   - drop open rides whose known wait is below minWaitMinutes
//     (unknown waits, waitTime < 0, are kept — no data isn't a short wait)
//   - SAFETY: if filtering would leave nothing, revert to the unfiltered
//     input so the all-rides-closed park screen still works
//   - stable sort: favorites first (when favoritesFirst), then by wait
//     descending with closed rides last (when sortMode == SORT_MODE_WAIT_DESC)
void applyDisplayOptions(RideInfo rides[], int& count,
                         const RideDisplayOptions& opt);

#endif // RIDEFILTER_H
