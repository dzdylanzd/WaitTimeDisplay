#ifndef WAITLEVEL_H
#define WAITLEVEL_H

// Shared wait-time severity buckets. The display's colour themes and the
// status LED both map from this, so the level logic lives in exactly one
// place. The thresholds default to 15/30/45 min but are user-configurable
// (RuntimeConfig::waitTh1..3) — callers with access to the config pass them.
// (Mixed-case values on purpose — ALL-CAPS names collide with Windows macros
//  in the simulator build.)
enum class WaitLevel { Green, Amber, Orange, Red, Closed };

inline WaitLevel pickWaitLevel(int waitTime, bool isOpen,
                               int th1 = 15, int th2 = 30, int th3 = 45) {
  if (!isOpen)         return WaitLevel::Closed;
  if (waitTime <= th1) return WaitLevel::Green;
  if (waitTime <= th2) return WaitLevel::Amber;
  if (waitTime <= th3) return WaitLevel::Orange;
  return WaitLevel::Red;
}

#endif // WAITLEVEL_H
