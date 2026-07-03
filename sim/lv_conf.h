// lv_conf.h for the desktop simulator.
//
// Includes the real (project-root) lv_conf.h unchanged and overrides only
// the one setting that differs for desktop — LV_COLOR_16_SWAP (SDL2 handles
// byte order natively; the ESP32/SPI-DMA swap isn't needed here). Every
// other LVGL setting (fonts, widgets, memory, etc.) is defined exactly once
// in the root file, so it can't silently drift between sim and firmware.

#include "../lv_conf.h"

#undef LV_COLOR_16_SWAP
#define LV_COLOR_16_SWAP 0   // SDL2 native byte order; do NOT swap
