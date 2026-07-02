#ifndef QUIETHOURS_H
#define QUIETHOURS_H

// True when nowMin (minutes since midnight) falls inside [startMin, endMin),
// handling windows that wrap past midnight (start > end, e.g. 22:00–07:00).
// start == end means the window is empty (never quiet), not "always quiet" —
// a slider accidentally left at the same value shouldn't black out the screen.
inline bool inQuietWindow(int nowMin, int startMin, int endMin) {
  if (startMin == endMin) return false;
  if (startMin < endMin)  return nowMin >= startMin && nowMin < endMin;
  return nowMin >= startMin || nowMin < endMin;   // overnight wrap
}

#endif // QUIETHOURS_H
