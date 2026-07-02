#pragma once
#include <Arduino.h>
#include <SPI.h>

// Physical panel: 172 columns × 320 rows
// We operate in LANDSCAPE: 320 px wide × 172 px tall
#define LCD_W  320
#define LCD_H  172

// SPI pins (Waveshare ESP32-C6-LCD-1.47)
#define LCD_PIN_MISO  5
#define LCD_PIN_MOSI  6
#define LCD_PIN_SCLK  7
#define LCD_PIN_CS   14
#define LCD_PIN_DC   15
#define LCD_PIN_RST  21
#define LCD_PIN_BL   22

// Panel row offset (240-column ST7789 RAM, 172-wide panel → offset 34 in portrait)
// In landscape (MV=1) the offset applies to the Y (row) address.
#define LCD_ROW_OFFSET 34

#define LCD_SPI_FREQ  80000000UL

// Backlight PWM
#define LCD_BL_FREQ   1000
#define LCD_BL_RES    10     // 10-bit (0–1023)

void LCD_Init(void);
void LCD_SetWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
void LCD_WritePixels(uint16_t* color, uint32_t numPixels);
void LCD_SetBacklight(uint8_t percent);   // 0–100
