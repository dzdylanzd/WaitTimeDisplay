/**
 * lv_conf.h — LVGL 8.3.x configuration for QueueWatch
 * Waveshare ESP32-C6-LCD-1.47 (172×320 ST7789, landscape = 320×172)
 *
 * Included via -DLV_CONF_INCLUDE_SIMPLE and -I"${PROJECT_SRC_DIR}".
 */
#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/*====================
   COLOR SETTINGS
 *====================*/

/* Color depth: 16 = RGB565, matching ST7789 native format */
#define LV_COLOR_DEPTH 16

/* Swap the 2 bytes of RGB565 color.
 * On little-endian ESP32, LVGL stores colors LSB-first in memory.
 * SPI DMA sends bytes in memory order, but ST7789 expects MSB first.
 * Setting 1 makes LVGL swap bytes before flushing → correct colours. */
#define LV_COLOR_16_SWAP 1

/* Enable full opacity support (no limitation on opa values) */
#define LV_COLOR_SCREEN_TRANSP 0

/* Chroma key color (used for transparent images) */
#define LV_COLOR_CHROMA_KEY lv_color_hex(0x00ff00)

/*=========================
   MEMORY SETTINGS
 *=========================*/

/* Use system malloc/free/realloc — plenty of heap on ESP32-C6 */
#define LV_MEM_CUSTOM 1
#if LV_MEM_CUSTOM == 1
  #define LV_MEM_CUSTOM_INCLUDE <stdlib.h>
  #define LV_MEM_CUSTOM_ALLOC   malloc
  #define LV_MEM_CUSTOM_FREE    free
  #define LV_MEM_CUSTOM_REALLOC realloc
#endif

/*====================
   HAL SETTINGS
 *====================*/

/* Default display refresh period in ms */
#define LV_DISP_DEF_REFR_PERIOD 10

/* Input device read period in ms (no touch on this board, set high) */
#define LV_INDEV_DEF_READ_PERIOD 100

/* Use external tick source — we drive lv_tick_inc() from esp_timer */
#define LV_TICK_CUSTOM 0

/* Default DPI (dots per inch) — 130 dpi is approximate for this small screen */
#define LV_DPI_DEF 130

/*=======================
   FEATURE CONFIGURATION
 *=======================*/

#define LV_USE_LOG 0        /* Disable LVGL log output (saves code size) */
#define LV_USE_PERF_MONITOR 0
#define LV_USE_MEM_MONITOR  0
#define LV_USE_REFR_DEBUG   0

/* Anti-aliasing (sub-pixel rendering) — leave off for pixel-exact renders */
#define LV_USE_ANTIALIAS 0  /* 0 = off; 1 = on (slower) */

/*=================
   FONT USAGE
 *=================*/

/* Built-in Montserrat fonts we use in QueueWatch */
#define LV_FONT_MONTSERRAT_14 1   /* ride index, sub-labels */
#define LV_FONT_MONTSERRAT_16 1   /* park name, time, ride name */
#define LV_FONT_MONTSERRAT_20 1   /* status screen titles */
#define LV_FONT_MONTSERRAT_28 1   /* "CLOSED" / fallback wait label */
#define LV_FONT_MONTSERRAT_36 1   /* wait-time number (large) */

/* Disable unused built-in fonts to save flash */
#define LV_FONT_MONTSERRAT_8  0
#define LV_FONT_MONTSERRAT_10 0
#define LV_FONT_MONTSERRAT_12 0
#define LV_FONT_MONTSERRAT_18 0
#define LV_FONT_MONTSERRAT_22 0
#define LV_FONT_MONTSERRAT_24 0
#define LV_FONT_MONTSERRAT_26 0
#define LV_FONT_MONTSERRAT_30 0
#define LV_FONT_MONTSERRAT_32 0
#define LV_FONT_MONTSERRAT_34 0
#define LV_FONT_MONTSERRAT_38 0
#define LV_FONT_MONTSERRAT_40 0
#define LV_FONT_MONTSERRAT_42 0
#define LV_FONT_MONTSERRAT_44 0
#define LV_FONT_MONTSERRAT_46 0
#define LV_FONT_MONTSERRAT_48 1
#define LV_FONT_UNSCII_8  0
#define LV_FONT_UNSCII_16 0
#define LV_FONT_DEJAVU_16_PERSIAN_HEBREW 0
#define LV_FONT_SIMSUN_16_CJK 0

/* Default font used by LVGL when no explicit font is set */
#define LV_FONT_DEFAULT &lv_font_montserrat_16

/* Enable UTF-8 encoding */
#define LV_TXT_ENC LV_TXT_ENC_UTF8

/* "…" character for long text */
#define LV_TXT_BREAK_CHARS " ,.;:-_"

/* Long text handling threshold */
#define LV_LABEL_LONG_TXT_HINT 0

/*====================
   WIDGET ENABLE
 *====================*/

#define LV_USE_ARC        0
#define LV_USE_BAR        1   /* progress bar */
#define LV_USE_BTN        0
#define LV_USE_BTNMATRIX  0
#define LV_USE_CANVAS     1   /* base for QR code widget */
#define LV_USE_CHECKBOX   0
#define LV_USE_DROPDOWN   0
#define LV_USE_IMG        1   /* base for canvas / QR code widget */
#define LV_USE_LABEL      1   /* all text labels */
#define LV_USE_LINE       0
#define LV_USE_ROLLER     0
#define LV_USE_SLIDER     0
#define LV_USE_SWITCH     0
#define LV_USE_TEXTAREA   0
#define LV_USE_TABLE      0
/* Extra widgets (LVGL 8.4 defaults these to 1; disable all we don't use) */
#define LV_USE_ANIMIMG    0   /* needs LV_USE_IMG — keep off */
#define LV_USE_CHART      0
#define LV_USE_COLORWHEEL 0
#define LV_USE_IMGBTN     0
#define LV_USE_KEYBOARD   0
#define LV_USE_LED        0
#define LV_USE_LIST       0
#define LV_USE_MENU       0
#define LV_USE_METER      0
#define LV_USE_MSGBOX     0
#define LV_USE_SPAN       0
#define LV_USE_SPINBOX    0
#define LV_USE_SPINNER    0
#define LV_USE_TABVIEW    0
#define LV_USE_TILEVIEW   0
#define LV_USE_WIN        0
#define LV_USE_CALENDAR   0

/*====================
   EXTRA LIBRARIES
 *====================*/

/* QR code shown on the Wi-Fi setup screen so a phone can auto-join the AP.
   Requires LV_USE_CANVAS + LV_USE_IMG (enabled above). */
#define LV_USE_QRCODE 1

/*====================
   THEMES
 *====================*/

/* We style everything manually — no built-in theme needed */
#define LV_USE_THEME_DEFAULT 0
#define LV_USE_THEME_BASIC   0
#define LV_USE_THEME_MONO    0

/*====================
   LAYOUTS
 *====================*/

/* Not using flex or grid layouts — absolute positioning only */
#define LV_USE_FLEX 0
#define LV_USE_GRID 0

/*====================
   MISC
 *====================*/

#define LV_SPRINTF_CUSTOM 0
#define LV_USE_USER_DATA  1
#define LV_USE_ASSERT_NULL       0
#define LV_USE_ASSERT_MALLOC     0
#define LV_USE_ASSERT_STYLE      0
#define LV_USE_ASSERT_MEM_INTEGRITY 0
#define LV_USE_ASSERT_OBJ        0

#define LV_DRAW_COMPLEX 1     /* needed for radius/opacity */
#define LV_SHADOW_CACHE_SIZE 0
#define LV_CIRCLE_CACHE_SIZE 4
#define LV_IMG_CACHE_DEF_SIZE 1
#define LV_GRADIENT_MAX_STOPS 2
#define LV_GRAD_CACHE_DEF_SIZE 0

#define LV_USE_GPU_STM32_DMA2D 0
#define LV_USE_GPU_NXP_PXP     0
#define LV_USE_GPU_NXP_VG_LITE  0
#define LV_USE_GPU_SDL          0

#define LV_USE_ANIMATION 1    /* needed for label scroll, screen fade transitions */

#define LV_DISP_ROT_MAX_BUF (10*1024)

#endif /* LV_CONF_H */
