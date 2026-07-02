// Simulator stub for the ST7789 LCD driver.
// The real pixel data flows through LVGL's flush callback (lvgl_driver_sim.cpp)
// so these functions are no-ops.
#include "../src/lcd_st7789.h"

void LCD_Init()                                               {}
void LCD_SetWindow(uint16_t, uint16_t, uint16_t, uint16_t)   {}
void LCD_WritePixels(uint16_t*, uint32_t)                     {}
void LCD_SetBacklight(uint8_t)                                {}
void LCD_SetRotation(bool)                                    {}
