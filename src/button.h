#ifndef BUTTON_H
#define BUTTON_H

#include <Arduino.h>

enum class ButtonEvent {
  None,
  Short,        // released before the long-press threshold → next ride
  Long,         // held 700 ms (fires once, while still held) → next park
  HoldWarning,  // held 10 s → show the factory-reset warning screen
  HoldReset,    // held 20 s → perform the factory reset
  HoldCancel    // released between warning and reset → abort, restore screen
};

// Debounced push button with short/long/very-long press detection. Each
// threshold fires ONCE the moment the hold reaches it (immediate feedback
// while still holding); the release after Long or HoldReset is swallowed.
// update() is a pure state machine (testable off-hardware); poll() is the
// thin wrapper that reads the pin (active-low, INPUT_PULLUP).
class Button {
public:
  static constexpr unsigned long DEBOUNCE_MS   = 30;
  static constexpr unsigned long LONGPRESS_MS  = 700;
  static constexpr unsigned long HOLD_WARN_MS  = 10000;
  static constexpr unsigned long HOLD_RESET_MS = 20000;

  void begin(uint8_t pin);
  ButtonEvent poll(unsigned long now);
  ButtonEvent update(bool pressed, unsigned long now);

private:
  uint8_t       _pin          = 255;
  bool          _stablePressed = false;   // debounced state
  bool          _rawLast       = false;
  unsigned long _rawChangeTime = 0;
  unsigned long _pressStart    = 0;
  bool          _longFired     = false;
  bool          _warnFired     = false;
  bool          _resetFired    = false;
};

#endif // BUTTON_H
