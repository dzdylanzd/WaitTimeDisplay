#ifndef STATUSLED_H
#define STATUSLED_H

#include <Arduino.h>
#include "waitlevel.h"

// Onboard WS2812 RGB LED: mirrors the current ride's wait-level colour so
// the wait severity is readable from across the room. Brightness follows the
// display's backlight setting (including quiet hours).
class StatusLed {
public:
  void begin();
  void showLevel(WaitLevel level, uint8_t brightnessPct);
  void off();

private:
  static void writeRGB(uint8_t r, uint8_t g, uint8_t b);
  bool    _isOff     = true;
  WaitLevel _lastLevel = WaitLevel::Green;
  uint8_t _lastBrt   = 255;
};

#endif // STATUSLED_H
