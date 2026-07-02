#ifndef WAITLEVEL_H
#define WAITLEVEL_H

// Shared wait-time severity buckets. The display's colour themes and the
// status LED both map from this, so the thresholds live in exactly one place.
// (Mixed-case values on purpose — ALL-CAPS names collide with Windows macros
//  in the simulator build.)
enum class WaitLevel { Green, Amber, Orange, Red, Closed };

inline WaitLevel pickWaitLevel(int waitTime, bool isOpen) {
  if (!isOpen)        return WaitLevel::Closed;
  if (waitTime <= 15) return WaitLevel::Green;
  if (waitTime <= 30) return WaitLevel::Amber;
  if (waitTime <= 45) return WaitLevel::Orange;
  return WaitLevel::Red;
}

#endif // WAITLEVEL_H
