#include "statusled.h"
#include "config.h"

void StatusLed::setColors(const uint32_t colors[5]) {
  for (int i = 0; i < 5; i++) _colors[i] = colors[i];
  _isOff = true;   // defeat the showLevel() dedupe so the new colour paints
}

void StatusLed::begin() {
  writeRGB(0, 0, 0);   // rgbLedWrite needs no pin setup, just start dark
}

void StatusLed::showLevel(WaitLevel level, uint8_t brightnessPct) {
  if (brightnessPct == 0) { off(); return; }
  if (!_isOff && level == _lastLevel && brightnessPct == _lastBrt) return;

  uint32_t c = _colors[(int)level];
  uint8_t r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
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
  rgbLedWrite(RGB_LED_PIN, r, g, b);   // ESP32 core builtin (RMT-driven)
}
