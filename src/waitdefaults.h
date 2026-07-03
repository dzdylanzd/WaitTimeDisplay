#ifndef WAITDEFAULTS_H
#define WAITDEFAULTS_H

#include <stdint.h>

// Single source of truth for the wait-level defaults, shared by
// RuntimeConfig (configmanager.h), StatusLed (statusled.h), the display's
// WAIT_THEMES/thresholds (display.cpp) and waitlevel.h's pickWaitLevel()
// default arguments. Indexed by (int)WaitLevel: Green/Amber/Orange/Red/Closed.
static constexpr uint32_t WAIT_COLOR_DEFAULTS[5] = {
  0x00E676, 0xFFD600, 0xFF7043, 0xFF1744, 0x18FFFF
};

// "short" up to N minutes / "medium" up to N / "long" up to N; above = red.
static constexpr uint8_t WAIT_TH_DEFAULTS[3] = { 15, 30, 45 };

#endif // WAITDEFAULTS_H
