#include "button.h"

void Button::begin(uint8_t pin) {
  _pin = pin;
  pinMode(_pin, INPUT_PULLUP);   // BOOT button pulls the pin to GND
}

ButtonEvent Button::poll(unsigned long now) {
  if (_pin == 255) return ButtonEvent::None;
  return update(digitalRead(_pin) == LOW, now);
}

ButtonEvent Button::update(bool pressed, unsigned long now) {
  // Debounce: track the raw signal and only accept a new stable state after
  // it has held still for DEBOUNCE_MS.
  if (pressed != _rawLast) {
    _rawLast = pressed;
    _rawChangeTime = now;
  }
  if (pressed != _stablePressed && now - _rawChangeTime >= DEBOUNCE_MS) {
    _stablePressed = pressed;
    if (pressed) {
      _pressStart = _rawChangeTime;   // the press began at the raw edge
      _longFired  = false;
      _warnFired  = false;
      _resetFired = false;
    } else if (_warnFired && !_resetFired) {
      return ButtonEvent::HoldCancel;   // let go during the warning window
    } else if (!_longFired && !_warnFired && !_resetFired) {
      return ButtonEvent::Short;   // release before the long threshold
    }
  }

  if (_stablePressed) {
    unsigned long held = now - _pressStart;
    if (!_resetFired && held >= HOLD_RESET_MS) {
      _resetFired = true;
      _warnFired  = true;   // in case polling skipped straight past 10 s
      return ButtonEvent::HoldReset;
    }
    if (!_warnFired && held >= HOLD_WARN_MS) {
      _warnFired = true;
      return ButtonEvent::HoldWarning;
    }
    if (!_longFired && held >= LONGPRESS_MS) {
      _longFired = true;
      return ButtonEvent::Long;
    }
  }
  return ButtonEvent::None;
}
