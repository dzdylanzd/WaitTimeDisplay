#pragma once

#include <lvgl.h>
#include "lcd_st7789.h"

// LVGL draw buffer: 1/10 of screen pixels (320×172 / 10 = 5504 pixels per buf)
// Double-buffered for smoother flushing
#define LVGL_BUF_PIXELS (LCD_W * LCD_H / 10)

// LVGL tick period driven by esp_timer (ms)
#define LVGL_TICK_MS 5

/**
 * Initialise LVGL, register the ST7789 display driver, and start the
 * esp_timer that feeds lv_tick_inc() every LVGL_TICK_MS milliseconds.
 *
 * Call once from DisplayController::begin() after LCD_Init().
 * After this returns, call lv_timer_handler() on every loop() iteration.
 */
void Lvgl_Init(void);

// Flush callback — called by LVGL to push a dirty rectangle to the LCD
void Lvgl_FlushCB(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* buf);

// esp_timer callback — increments the LVGL tick counter
void Lvgl_TickCB(void* arg);
