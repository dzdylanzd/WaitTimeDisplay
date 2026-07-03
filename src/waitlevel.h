#ifndef WAITLEVEL_H
#define WAITLEVEL_H

#include "waitdefaults.h"

// Shared wait-time severity buckets. The display's colour themes and the
// status LED both map from this, so the level logic lives in exactly one
// place. The thresholds default to WAIT_TH_DEFAULTS (waitdefaults.h) but are
// user-configurable (RuntimeConfig::waitTh1..3) — callers with access to the
// config pass them.
// (Mixed-case values on purpose — ALL-CAPS names collide with Windows macros
//  in the simulator build.)
enum class WaitLevel { Green, Amber, Orange, Red, Closed };

inline WaitLevel pickWaitLevel(int waitTime, bool isOpen,
                               int th1 = WAIT_TH_DEFAULTS[0],
                               int th2 = WAIT_TH_DEFAULTS[1],
                               int th3 = WAIT_TH_DEFAULTS[2]) {
  if (!isOpen)         return WaitLevel::Closed;
  if (waitTime <= th1) return WaitLevel::Green;
  if (waitTime <= th2) return WaitLevel::Amber;
  if (waitTime <= th3) return WaitLevel::Orange;
  return WaitLevel::Red;
}

#endif // WAITLEVEL_H
