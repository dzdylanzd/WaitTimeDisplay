// lv_conf.h for the desktop simulator.
// Must stay in sync with the project-root lv_conf.h EXCEPT:
//   - LV_COLOR_16_SWAP 0  (SDL2 handles endianness; no byte swap needed)
//   - LV_MEM_SIZE set to something large for desktop

#ifndef LV_CONF_H
#define LV_CONF_H
#include <stdint.h>

#define LV_COLOR_DEPTH      16
#define LV_COLOR_16_SWAP     0   // ← SDL2 native byte order; do NOT swap
#define LV_COLOR_SCREEN_TRANSP 0
#define LV_COLOR_CHROMA_KEY lv_color_hex(0x00ff00)

#define LV_MEM_CUSTOM 1
#if LV_MEM_CUSTOM
  #define LV_MEM_CUSTOM_INCLUDE <stdlib.h>
  #define LV_MEM_CUSTOM_ALLOC   malloc
  #define LV_MEM_CUSTOM_FREE    free
  #define LV_MEM_CUSTOM_REALLOC realloc
#endif

#define LV_DISP_DEF_REFR_PERIOD 10
#define LV_INDEV_DEF_READ_PERIOD 100
#define LV_TICK_CUSTOM 0
#define LV_DPI_DEF 130

#define LV_USE_LOG 0
#define LV_USE_PERF_MONITOR 0
#define LV_USE_MEM_MONITOR  0
#define LV_USE_REFR_DEBUG   0
#define LV_USE_ANTIALIAS    0

// Fonts — must match production to keep display.cpp identical
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_MONTSERRAT_36 1
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
#define LV_FONT_DEFAULT &lv_font_montserrat_16
#define LV_TXT_ENC LV_TXT_ENC_UTF8
#define LV_TXT_BREAK_CHARS " ,.;:-_"
#define LV_LABEL_LONG_TXT_HINT 0

// Widgets — match production
#define LV_USE_ARC        0
#define LV_USE_BAR        1
#define LV_USE_BTN        0
#define LV_USE_BTNMATRIX  0
#define LV_USE_CANVAS     1   /* base for QR code widget */
#define LV_USE_CHECKBOX   0
#define LV_USE_DROPDOWN   0
#define LV_USE_IMG        1   /* base for canvas / QR code widget */
#define LV_USE_LABEL      1
#define LV_USE_LINE       0
#define LV_USE_ROLLER     0
#define LV_USE_SLIDER     0
#define LV_USE_SWITCH     0
#define LV_USE_TEXTAREA   0
#define LV_USE_TABLE      0
#define LV_USE_ANIMIMG    0
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

/* QR code shown on the Wi-Fi setup screen (needs LV_USE_CANVAS + LV_USE_IMG). */
#define LV_USE_QRCODE 1

#define LV_USE_THEME_DEFAULT 0
#define LV_USE_THEME_BASIC   0
#define LV_USE_THEME_MONO    0

#define LV_USE_FLEX 0
#define LV_USE_GRID 0

#define LV_SPRINTF_CUSTOM 0
#define LV_USE_USER_DATA  1
#define LV_USE_ASSERT_NULL          0
#define LV_USE_ASSERT_MALLOC        0
#define LV_USE_ASSERT_STYLE         0
#define LV_USE_ASSERT_MEM_INTEGRITY 0
#define LV_USE_ASSERT_OBJ           0

#define LV_DRAW_COMPLEX      1
#define LV_SHADOW_CACHE_SIZE 0
#define LV_CIRCLE_CACHE_SIZE 4
#define LV_IMG_CACHE_DEF_SIZE 1
#define LV_GRADIENT_MAX_STOPS 2
#define LV_GRAD_CACHE_DEF_SIZE 0

#define LV_USE_GPU_STM32_DMA2D 0
#define LV_USE_GPU_NXP_PXP     0
#define LV_USE_GPU_NXP_VG_LITE  0
#define LV_USE_GPU_SDL          0

#define LV_USE_ANIMATION 1
#define LV_DISP_ROT_MAX_BUF (10*1024)

#endif // LV_CONF_H
