#include "statusled.h"
#include "config.h"

// The WS2812 is far brighter than the LCD backlight at the same duty, so cap
// it well below full scale and let brightnessPct scale up from there.
static constexpr uint16_t LED_MAX_DUTY = 64;   // out of 255

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
  uint16_t scale = (uint16_t)brightnessPct * LED_MAX_DUTY / 100;
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
