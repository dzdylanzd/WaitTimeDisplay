#ifndef STATUSLED_H
#define STATUSLED_H

#include <Arduino.h>
#include "waitlevel.h"
#include "waitdefaults.h"

// Onboard WS2812 RGB LED: mirrors the current ride's wait-level colour so
// the wait severity is readable from across the room. Brightness follows the
// display's backlight setting (including quiet hours).
class StatusLed {
public:
  void begin();
  void showLevel(WaitLevel level, uint8_t brightnessPct);
  void off();

  // User-configured level colours (0xRRGGBB, indexed by (int)WaitLevel) —
  // shared with the display's wait themes via RuntimeConfig::waitColors.
  void setColors(const uint32_t colors[5]);

private:
  static void writeRGB(uint8_t r, uint8_t g, uint8_t b);
  bool    _isOff     = true;
  WaitLevel _lastLevel = WaitLevel::Green;
  uint8_t _lastBrt   = 255;
  // Defaults come from waitdefaults.h (shared with RuntimeConfig::waitColors)
  uint32_t _colors[5] = {
    WAIT_COLOR_DEFAULTS[0], WAIT_COLOR_DEFAULTS[1], WAIT_COLOR_DEFAULTS[2],
    WAIT_COLOR_DEFAULTS[3], WAIT_COLOR_DEFAULTS[4]
  };
};

#endif // STATUSLED_H
