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
//   - skipClosed drops everything not currently ridable/watchable: closed,
//     down and refurbishing attractions, and shows with no showtime left
//     today (user decision: a DOWN ride is hidden too, not surfaced)
//   - drop open attractions whose known wait is below minWaitMinutes
//     (unknown waits, waitTime < 0, are kept — no data isn't a short wait;
//     shows are exempt — they have no wait)
//   - SAFETY: if filtering would leave nothing, revert to the unfiltered
//     input so the all-rides-closed park screen still works
//   - stable sort: favorites first (when favoritesFirst), then by wait
//     descending with down/closed rides last (when SORT_MODE_WAIT_DESC);
//     shows rank between real waits and closed, by time until showtime
void applyDisplayOptions(RideInfo rides[], int& count,
                         const RideDisplayOptions& opt);

#endif // RIDEFILTER_H
