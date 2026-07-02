#include "statusled.h"
#include "config.h"

// The wait-level colours mirror the display themes' accents (display.cpp).
static void levelToRGB(WaitLevel level, uint8_t& r, uint8_t& g, uint8_t& b) {
  switch (level) {
    case WaitLevel::Green:  r = 0x00; g = 0xE6; b = 0x76; break;
    case WaitLevel::Amber:  r = 0xFF; g = 0xD6; b = 0x00; break;
    case WaitLevel::Orange: r = 0xFF; g = 0x70; b = 0x43; break;
    case WaitLevel::Red:    r = 0xFF; g = 0x17; b = 0x44; break;
    case WaitLevel::Closed: r = 0x18; g = 0xFF; b = 0xFF; break;
    default:                r = 0;    g = 0;    b = 0;    break;
  }
}

void StatusLed::begin() {
  writeRGB(0, 0, 0);   // neopixelWrite needs no pin setup, just start dark
}

void StatusLed::showLevel(WaitLevel level, uint8_t brightnessPct) {
  if (brightnessPct == 0) { off(); return; }
  if (!_isOff && level == _lastLevel && brightnessPct == _lastBrt) return;

  uint8_t r, g, b;
  levelToRGB(level, r, g, b);
  // The WS2812 is far brighter than the LCD backlight at the same duty, so
  // cap it at ~1/4 and scale with the display brightness from there.
  uint16_t scale = (uint16_t)brightnessPct * 64 / 100;   // 0–64 of 255
  writeRGB((uint8_t)(r * scale / 255),
           (uint8_t)(g * scale / 255),
           (uint8_t)(b * scale / 255));
  _isOff     = false;
  _lastLevel = level;
  _lastBrt   = brightnessPct;
}

void StatusLed::off() {
  if (_isOff) return;
  writeRGB(0, 0, 0);
  _isOff = true;
}

void StatusLed::writeRGB(uint8_t r, uint8_t g, uint8_t b) {
  neopixelWrite(RGB_LED_PIN, r, g, b);   // ESP32 core builtin (RMT-driven)
}
