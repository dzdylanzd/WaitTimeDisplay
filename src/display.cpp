#include "display.h"
#include "lcd_st7789.h"
#include "lvgl_driver.h"
#include "tzhelper.h"
#include "waitlevel.h"
#include "waitdefaults.h"
#include "configmanager.h"

// ---------------------------------------------------------------------------
// UI colour palettes — user-selectable in the web UI (NVS key "pal").
//
// A palette styles the "chrome" of the UI: header, separators, text and
// panel backgrounds. The wait-time themes further down are deliberately NOT
// part of the palette — green/amber/orange/red/teal carry meaning and must
// look the same in every palette.
//
// The palette NAMES live in the web UI (PALETTE_DEFS in cfgserver.cpp) — keep
// that list's length matching PALETTES[] below (a static_assert further down
// enforces the count against configmanager.h's COLOR_PALETTE_COUNT, so a
// display/config mismatch fails the build instead of diverging silently).
// ---------------------------------------------------------------------------
static const lv_color_t C_BLACK     = LV_COLOR_MAKE(0x00, 0x00, 0x00);
static const lv_color_t C_WHITE     = LV_COLOR_MAKE(0xFF, 0xFF, 0xFF);

struct UiPalette {
    lv_color_t hdrBg;      // header panel background
    lv_color_t accent;     // separators / trim ("gold"), fresh data
    lv_color_t accentDim;  // separator, 5–14 min old data
    lv_color_t accentOld;  // separator, 15+ min old data
    lv_color_t track;      // progress bar track
    lv_color_t parkTxt;    // park name + IP/URL text
    lv_color_t timeTxt;    // header clock
    lv_color_t idxTxt;     // muted: "3/12", land, country
    lv_color_t rideTxt;    // ride name
    lv_color_t bodyTxt;    // body / status text
    lv_color_t rideBg;     // ride panel background
    lv_color_t panelBg;    // bottom status/portal bar background
    lv_color_t statusBg;   // status + portal screen background
};

// Deliberately saturated header/panel hues so switching palettes is
// unmistakable on the small screen. Keep the swatch hexes in the web UI
// (PALETTE_DEFS in cfgserver.cpp: h=hdrBg, a=accent, p=rideBg) matching
// these values.
static const UiPalette PALETTES[] = {
  // ── Strong / saturated ──────────────────────────────────────
  // 0 — Magic Night (default): vivid indigo/purple, warm gold trim
  { LV_COLOR_MAKE(0x34,0x10,0x5F), LV_COLOR_MAKE(0xD9,0xA6,0x2E),
    LV_COLOR_MAKE(0x7B,0x4A,0x18), LV_COLOR_MAKE(0x50,0x18,0x10),
    LV_COLOR_MAKE(0x20,0x18,0x40), LV_COLOR_MAKE(0xFF,0xD4,0x66),
    LV_COLOR_MAKE(0x8C,0x9E,0xEE), LV_COLOR_MAKE(0x6E,0x7C,0xBE),
    LV_COLOR_MAKE(0xEA,0xEA,0xFF), LV_COLOR_MAKE(0x96,0x96,0xC4),
    LV_COLOR_MAKE(0x16,0x0A,0x34), LV_COLOR_MAKE(0x12,0x08,0x30),
    LV_COLOR_MAKE(0x20,0x0C,0x40) },
  // 1 — Deep Ocean: azure blues, cyan trim
  { LV_COLOR_MAKE(0x06,0x35,0x5F), LV_COLOR_MAKE(0x22,0xC8,0xE0),
    LV_COLOR_MAKE(0x11,0x70,0x7F), LV_COLOR_MAKE(0x50,0x20,0x14),
    LV_COLOR_MAKE(0x0E,0x27,0x40), LV_COLOR_MAKE(0x7D,0xF3,0xE8),
    LV_COLOR_MAKE(0x7F,0xB8,0xDF), LV_COLOR_MAKE(0x52,0x7F,0xA6),
    LV_COLOR_MAKE(0xE6,0xF4,0xFF), LV_COLOR_MAKE(0x86,0xA6,0xC2),
    LV_COLOR_MAKE(0x0B,0x1E,0x33), LV_COLOR_MAKE(0x08,0x18,0x2C),
    LV_COLOR_MAKE(0x04,0x24,0x40) },
  // 2 — Sunset Ember: crimson-ember, orange trim
  { LV_COLOR_MAKE(0x7A,0x21,0x11), LV_COLOR_MAKE(0xFF,0x8A,0x1E),
    LV_COLOR_MAKE(0x8A,0x4C,0x0E), LV_COLOR_MAKE(0x58,0x14,0x10),
    LV_COLOR_MAKE(0x38,0x18,0x10), LV_COLOR_MAKE(0xFF,0xB4,0x5C),
    LV_COLOR_MAKE(0xE8,0x96,0x78), LV_COLOR_MAKE(0xB0,0x6A,0x54),
    LV_COLOR_MAKE(0xFF,0xEE,0xE2), LV_COLOR_MAKE(0xC0,0x8E,0x7E),
    LV_COLOR_MAKE(0x25,0x12,0x0C), LV_COLOR_MAKE(0x20,0x0E,0x08),
    LV_COLOR_MAKE(0x35,0x14,0x0A) },
  // 3 — Forest Twilight: rich greens, lime trim
  { LV_COLOR_MAKE(0x0E,0x4D,0x2A), LV_COLOR_MAKE(0xA5,0xEA,0x34),
    LV_COLOR_MAKE(0x50,0x74,0x14), LV_COLOR_MAKE(0x58,0x20,0x10),
    LV_COLOR_MAKE(0x14,0x2C,0x1C), LV_COLOR_MAKE(0xC2,0xE8,0x6E),
    LV_COLOR_MAKE(0x8E,0xC8,0xA2), LV_COLOR_MAKE(0x5E,0x8E,0x70),
    LV_COLOR_MAKE(0xEA,0xF6,0xEC), LV_COLOR_MAKE(0x8E,0xAE,0x98),
    LV_COLOR_MAKE(0x10,0x26,0x1A), LV_COLOR_MAKE(0x0C,0x20,0x14),
    LV_COLOR_MAKE(0x0E,0x2E,0x18) },
  // 4 — Carbon Mono: graphite greys, silver trim
  { LV_COLOR_MAKE(0x3A,0x3A,0x40), LV_COLOR_MAKE(0xE6,0xE6,0xEA),
    LV_COLOR_MAKE(0x74,0x74,0x78), LV_COLOR_MAKE(0x58,0x1C,0x14),
    LV_COLOR_MAKE(0x28,0x28,0x2C), LV_COLOR_MAKE(0xF2,0xF2,0xF4),
    LV_COLOR_MAKE(0xB4,0xB4,0xBC), LV_COLOR_MAKE(0x8A,0x8A,0x92),
    LV_COLOR_MAKE(0xF6,0xF6,0xF8), LV_COLOR_MAKE(0xA6,0xA6,0xAE),
    LV_COLOR_MAKE(0x22,0x22,0x28), LV_COLOR_MAKE(0x1A,0x1A,0x20),
    LV_COLOR_MAKE(0x26,0x26,0x2C) },
  // 5 — Daylight (light): paper whites, deep-teal ink & trim.
  { LV_COLOR_MAKE(0xD9,0xE7,0xF7), LV_COLOR_MAKE(0x0D,0x7F,0xA9),
    LV_COLOR_MAKE(0x5E,0x93,0xB0), LV_COLOR_MAKE(0xB0,0x66,0x3A),
    LV_COLOR_MAKE(0xC2,0xD2,0xE6), LV_COLOR_MAKE(0x0C,0x4A,0x70),
    LV_COLOR_MAKE(0x3C,0x58,0x78), LV_COLOR_MAKE(0x5A,0x6B,0x84),
    LV_COLOR_MAKE(0x17,0x22,0x2E), LV_COLOR_MAKE(0x26,0x33,0x3F),
    LV_COLOR_MAKE(0xB8,0xCD,0xE8), LV_COLOR_MAKE(0xCE,0xDD,0xF0),
    LV_COLOR_MAKE(0xE0,0xEB,0xF6) },
  // 6 — Sandstone (light): warm ivory paper, burnt-amber ink & trim.
  { LV_COLOR_MAKE(0xF2,0xE6,0xCF), LV_COLOR_MAKE(0xC2,0x6B,0x18),
    LV_COLOR_MAKE(0xB0,0x89,0x5A), LV_COLOR_MAKE(0x9A,0x52,0x30),
    LV_COLOR_MAKE(0xE4,0xD6,0xC0), LV_COLOR_MAKE(0x6E,0x3A,0x0E),
    LV_COLOR_MAKE(0x6A,0x52,0x36), LV_COLOR_MAKE(0x7C,0x6A,0x52),
    LV_COLOR_MAKE(0x2A,0x20,0x16), LV_COLOR_MAKE(0x3A,0x2E,0x20),
    LV_COLOR_MAKE(0xDC,0xCD,0xB0), LV_COLOR_MAKE(0xE4,0xD8,0xC2),
    LV_COLOR_MAKE(0xEE,0xE4,0xD4) },
  // ── Soft / muted ────────────────────────────────────────────
  // 7 — Midnight Peony: charcoal plum, blush trim
  { LV_COLOR_MAKE(0x2B,0x24,0x38), LV_COLOR_MAKE(0xD7,0xA3,0xB3),
    LV_COLOR_MAKE(0x6B,0x4E,0x5E), LV_COLOR_MAKE(0x48,0x20,0x18),
    LV_COLOR_MAKE(0x20,0x1C,0x2C), LV_COLOR_MAKE(0xEB,0xD0,0xD6),
    LV_COLOR_MAKE(0xA0,0x84,0x96), LV_COLOR_MAKE(0x7E,0x68,0x78),
    LV_COLOR_MAKE(0xF0,0xE6,0xEE), LV_COLOR_MAKE(0xA8,0x94,0xA4),
    LV_COLOR_MAKE(0x1A,0x16,0x22), LV_COLOR_MAKE(0x14,0x12,0x1C),
    LV_COLOR_MAKE(0x22,0x1C,0x2A) },
  // 8 — Lagoon Fog: deep teal, mist trim
  { LV_COLOR_MAKE(0x23,0x48,0x5A), LV_COLOR_MAKE(0x9B,0xC7,0xC1),
    LV_COLOR_MAKE(0x50,0x7A,0x7E), LV_COLOR_MAKE(0x40,0x20,0x18),
    LV_COLOR_MAKE(0x1C,0x2C,0x38), LV_COLOR_MAKE(0xC4,0xE2,0xDE),
    LV_COLOR_MAKE(0x8A,0xB2,0xB6), LV_COLOR_MAKE(0x68,0x88,0x90),
    LV_COLOR_MAKE(0xE8,0xF4,0xF2), LV_COLOR_MAKE(0x94,0xB0,0xB6),
    LV_COLOR_MAKE(0x17,0x2D,0x37), LV_COLOR_MAKE(0x12,0x26,0x2E),
    LV_COLOR_MAKE(0x1C,0x34,0x40) },
  // 9 — Tea Rose: dusty rose, cream trim
  { LV_COLOR_MAKE(0x5C,0x34,0x41), LV_COLOR_MAKE(0xE4,0xC3,0xB8),
    LV_COLOR_MAKE(0x78,0x56,0x60), LV_COLOR_MAKE(0x48,0x20,0x18),
    LV_COLOR_MAKE(0x2C,0x1C,0x24), LV_COLOR_MAKE(0xF0,0xE0,0xD8),
    LV_COLOR_MAKE(0xA8,0x86,0x90), LV_COLOR_MAKE(0x84,0x68,0x72),
    LV_COLOR_MAKE(0xF6,0xEC,0xE8), LV_COLOR_MAKE(0xA4,0x8E,0x94),
    LV_COLOR_MAKE(0x35,0x20,0x28), LV_COLOR_MAKE(0x2C,0x1A,0x24),
    LV_COLOR_MAKE(0x40,0x26,0x30) },
  //10 — Willow Shade: olive, sage trim
  { LV_COLOR_MAKE(0x39,0x47,0x36), LV_COLOR_MAKE(0xB8,0xC4,0x9A),
    LV_COLOR_MAKE(0x62,0x7A,0x5A), LV_COLOR_MAKE(0x40,0x20,0x18),
    LV_COLOR_MAKE(0x24,0x2C,0x24), LV_COLOR_MAKE(0xD8,0xE0,0xCC),
    LV_COLOR_MAKE(0x96,0xAC,0x88), LV_COLOR_MAKE(0x74,0x88,0x68),
    LV_COLOR_MAKE(0xEE,0xF2,0xE8), LV_COLOR_MAKE(0x98,0xAA,0x90),
    LV_COLOR_MAKE(0x23,0x2D,0x21), LV_COLOR_MAKE(0x1C,0x26,0x1A),
    LV_COLOR_MAKE(0x2C,0x38,0x28) },
  //11 — Stone Harbor: slate blue, mist trim
  { LV_COLOR_MAKE(0x46,0x5A,0x70), LV_COLOR_MAKE(0xC7,0xD2,0xE1),
    LV_COLOR_MAKE(0x68,0x7E,0x98), LV_COLOR_MAKE(0x48,0x20,0x18),
    LV_COLOR_MAKE(0x2C,0x34,0x44), LV_COLOR_MAKE(0xE0,0xE8,0xF0),
    LV_COLOR_MAKE(0x9E,0xB2,0xC8), LV_COLOR_MAKE(0x7A,0x8C,0xA0),
    LV_COLOR_MAKE(0xEE,0xF2,0xF8), LV_COLOR_MAKE(0x9E,0xAE,0xC2),
    LV_COLOR_MAKE(0x2A,0x34,0x40), LV_COLOR_MAKE(0x22,0x2C,0x38),
    LV_COLOR_MAKE(0x34,0x40,0x4E) },
  //12 — Morning Clay: apricot, parchment trim
  { LV_COLOR_MAKE(0xD5,0xB0,0x8E), LV_COLOR_MAKE(0xF0,0xD9,0xB7),
    LV_COLOR_MAKE(0x9E,0x7E,0x64), LV_COLOR_MAKE(0x8A,0x4A,0x2E),
    LV_COLOR_MAKE(0xC4,0xA8,0x8E), LV_COLOR_MAKE(0x5E,0x36,0x18),
    LV_COLOR_MAKE(0x6A,0x54,0x40), LV_COLOR_MAKE(0x7C,0x6A,0x58),
    LV_COLOR_MAKE(0x2E,0x22,0x1A), LV_COLOR_MAKE(0x3A,0x2E,0x24),
    LV_COLOR_MAKE(0xB9,0x8F,0x6F), LV_COLOR_MAKE(0xC6,0xA2,0x84),
    LV_COLOR_MAKE(0xE0,0xCA,0xB4) },
  //13 — Sand Drift: warm taupe, linen trim
  { LV_COLOR_MAKE(0xBF,0xA7,0x8E), LV_COLOR_MAKE(0xE6,0xD7,0xC6),
    LV_COLOR_MAKE(0x8E,0x78,0x64), LV_COLOR_MAKE(0x78,0x44,0x2E),
    LV_COLOR_MAKE(0xAA,0x96,0x82), LV_COLOR_MAKE(0x50,0x30,0x1A),
    LV_COLOR_MAKE(0x5E,0x4E,0x3E), LV_COLOR_MAKE(0x6E,0x5E,0x50),
    LV_COLOR_MAKE(0x2A,0x22,0x1C), LV_COLOR_MAKE(0x36,0x2E,0x26),
    LV_COLOR_MAKE(0x8F,0x7A,0x64), LV_COLOR_MAKE(0xA0,0x8C,0x78),
    LV_COLOR_MAKE(0xBE,0xAE,0x9E) },
  // ── Dark accent ─────────────────────────────────────────────
  //14 — Night Iris: deep indigo, smoke accent
  { LV_COLOR_MAKE(0x2A,0x1E,0x4A), LV_COLOR_MAKE(0x16,0x15,0x1B),
    LV_COLOR_MAKE(0x0A,0x0A,0x0E), LV_COLOR_MAKE(0x30,0x10,0x10),
    LV_COLOR_MAKE(0x1C,0x14,0x30), LV_COLOR_MAKE(0xD9,0xA6,0x2E),
    LV_COLOR_MAKE(0x8C,0x9E,0xEE), LV_COLOR_MAKE(0x6E,0x7C,0xBE),
    LV_COLOR_MAKE(0xEA,0xEA,0xFF), LV_COLOR_MAKE(0x96,0x96,0xC4),
    LV_COLOR_MAKE(0x15,0x14,0x20), LV_COLOR_MAKE(0x10,0x10,0x1C),
    LV_COLOR_MAKE(0x1C,0x1A,0x30) },
  //15 — Dusk Harbor: storm blue, steel accent
  { LV_COLOR_MAKE(0x22,0x32,0x4B), LV_COLOR_MAKE(0x17,0x1B,0x22),
    LV_COLOR_MAKE(0x0C,0x0E,0x12), LV_COLOR_MAKE(0x30,0x10,0x10),
    LV_COLOR_MAKE(0x14,0x1C,0x30), LV_COLOR_MAKE(0x7D,0xF3,0xE8),
    LV_COLOR_MAKE(0x7F,0xB8,0xDF), LV_COLOR_MAKE(0x52,0x7F,0xA6),
    LV_COLOR_MAKE(0xE6,0xF4,0xFF), LV_COLOR_MAKE(0x86,0xA6,0xC2),
    LV_COLOR_MAKE(0x14,0x1C,0x28), LV_COLOR_MAKE(0x10,0x16,0x20),
    LV_COLOR_MAKE(0x1C,0x24,0x34) },
  //16 — Cedar Wine: cedar, burgundy accent
  { LV_COLOR_MAKE(0x3F,0x24,0x31), LV_COLOR_MAKE(0x1A,0x12,0x16),
    LV_COLOR_MAKE(0x0C,0x08,0x0C), LV_COLOR_MAKE(0x38,0x10,0x10),
    LV_COLOR_MAKE(0x24,0x14,0x20), LV_COLOR_MAKE(0xFF,0xB4,0x5C),
    LV_COLOR_MAKE(0xE8,0x96,0x78), LV_COLOR_MAKE(0xB0,0x6A,0x54),
    LV_COLOR_MAKE(0xFF,0xEE,0xE2), LV_COLOR_MAKE(0xC0,0x8E,0x7E),
    LV_COLOR_MAKE(0x20,0x16,0x1C), LV_COLOR_MAKE(0x1A,0x12,0x16),
    LV_COLOR_MAKE(0x2C,0x1A,0x22) },
  //17 — Moss Slate: moss, stone accent
  { LV_COLOR_MAKE(0x2D,0x3A,0x30), LV_COLOR_MAKE(0x15,0x19,0x17),
    LV_COLOR_MAKE(0x0A,0x0C,0x0C), LV_COLOR_MAKE(0x30,0x10,0x10),
    LV_COLOR_MAKE(0x1C,0x22,0x1C), LV_COLOR_MAKE(0xC2,0xE8,0x6E),
    LV_COLOR_MAKE(0x8E,0xC8,0xA2), LV_COLOR_MAKE(0x5E,0x8E,0x70),
    LV_COLOR_MAKE(0xEA,0xF6,0xEC), LV_COLOR_MAKE(0x8E,0xAE,0x98),
    LV_COLOR_MAKE(0x19,0x1F,0x1B), LV_COLOR_MAKE(0x14,0x1A,0x16),
    LV_COLOR_MAKE(0x22,0x2A,0x24) },
  //18 — Ink Alloy: ink, alloy accent
  { LV_COLOR_MAKE(0x34,0x38,0x48), LV_COLOR_MAKE(0x16,0x19,0x20),
    LV_COLOR_MAKE(0x0A,0x0C,0x10), LV_COLOR_MAKE(0x30,0x10,0x10),
    LV_COLOR_MAKE(0x1C,0x1C,0x28), LV_COLOR_MAKE(0xF2,0xF2,0xF4),
    LV_COLOR_MAKE(0xB4,0xB4,0xBC), LV_COLOR_MAKE(0x8A,0x8A,0x92),
    LV_COLOR_MAKE(0xF6,0xF6,0xF8), LV_COLOR_MAKE(0xA6,0xA6,0xAE),
    LV_COLOR_MAKE(0x19,0x1C,0x25), LV_COLOR_MAKE(0x14,0x16,0x1E),
    LV_COLOR_MAKE(0x22,0x26,0x30) },
  //19 — Frost Pine: pine, frost accent
  { LV_COLOR_MAKE(0x24,0x42,0x3B), LV_COLOR_MAKE(0x12,0x18,0x17),
    LV_COLOR_MAKE(0x08,0x0C,0x0C), LV_COLOR_MAKE(0x30,0x10,0x10),
    LV_COLOR_MAKE(0x18,0x26,0x24), LV_COLOR_MAKE(0x7D,0xF3,0xE8),
    LV_COLOR_MAKE(0x7F,0xB8,0xDF), LV_COLOR_MAKE(0x52,0x7F,0xA6),
    LV_COLOR_MAKE(0xE6,0xF4,0xFF), LV_COLOR_MAKE(0x86,0xA6,0xC2),
    LV_COLOR_MAKE(0x16,0x25,0x20), LV_COLOR_MAKE(0x12,0x1E,0x1A),
    LV_COLOR_MAKE(0x1A,0x30,0x2A) },
  //20 — Desert Night: sand, shadow accent
  { LV_COLOR_MAKE(0x4A,0x3B,0x2D), LV_COLOR_MAKE(0x1A,0x15,0x11),
    LV_COLOR_MAKE(0x0C,0x0A,0x08), LV_COLOR_MAKE(0x38,0x10,0x10),
    LV_COLOR_MAKE(0x2A,0x22,0x1C), LV_COLOR_MAKE(0x6E,0x3A,0x0E),
    LV_COLOR_MAKE(0x6A,0x52,0x36), LV_COLOR_MAKE(0x7C,0x6A,0x52),
    LV_COLOR_MAKE(0x2A,0x20,0x16), LV_COLOR_MAKE(0x3A,0x2E,0x20),
    LV_COLOR_MAKE(0x25,0x1D,0x15), LV_COLOR_MAKE(0x1E,0x18,0x12),
    LV_COLOR_MAKE(0x34,0x28,0x1E) },
};
static constexpr int UI_PALETTE_COUNT = sizeof(PALETTES) / sizeof(PALETTES[0]);
// The last colorPalette index is the user-defined "Custom" palette, held in a
// separate mutable slot (s_customPalette) rather than a PALETTES[] entry — so
// the presets stay const. configmanager.h's COLOR_PALETTE_COUNT (validated by
// cfgserver.cpp and clamped by ConfigManager) is the presets + that one Custom.
static constexpr int CUSTOM_PALETTE_INDEX = UI_PALETTE_COUNT;
static_assert(UI_PALETTE_COUNT + 1 == COLOR_PALETTE_COUNT,
              "PALETTES[] count + 1 (Custom) must match COLOR_PALETTE_COUNT");
// The Custom palette, derived from 3 user colours by setCustomPalette();
// starts as a copy of the default preset so it is valid before first use.
static UiPalette s_customPalette = PALETTES[0];

// Active palette — every colour below is read through this pointer, so text
// and message colours set at call time always use the current palette.
static const UiPalette* PAL = &PALETTES[0];

#define C_HDR_L    (PAL->hdrBg)
#define C_GOLD     (PAL->accent)
#define C_GOLD_DIM (PAL->accentDim)
#define C_GOLD_OLD (PAL->accentOld)
#define C_SEP      (PAL->track)
#define C_PARK_TXT (PAL->parkTxt)
#define C_TIME_TXT (PAL->timeTxt)
#define C_IDX_TXT  (PAL->idxTxt)
#define C_RIDE_TXT (PAL->rideTxt)
#define C_BODY_TXT (PAL->bodyTxt)
#define C_RIDE_BG  (PAL->rideBg)
#define C_PANEL_BG (PAL->panelBg)
#define C_STAT_BG  (PAL->statusBg)

// ---------------------------------------------------------------------------
// Wait-time themes: { bgTop, bgBot (gradient), accent }
//
// The accent of each level is user-configurable (RuntimeConfig::waitColors,
// pushed in via setWaitConfig) and drives the big number, top border and
// stripe. The panel BACKGROUND is the active palette's signature accent colour
// (PAL->accent, see themeFromColor) — uniform across all palettes. The
// thresholds are user-configurable too (waitTh1..3).
// ---------------------------------------------------------------------------
struct WaitTheme { lv_color_t bgTop; lv_color_t bgBot; lv_color_t accent; };

static WaitTheme themeFromColor(uint32_t rgb) {
    uint8_t r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
    WaitTheme t;
    // The wait-panel background is the ACTIVE PALETTE's signature accent colour
    // (gold / cyan / teal …) — the same for every wait level, so each palette
    // gives a distinct, on-brand panel and the screen shows three palette
    // colours (header, ride panel, wait panel). Only the number, border and
    // stripe carry the configured wait colour.
    t.bgTop  = PAL->accent;
    t.bgBot  = PAL->accent;
    t.accent = lv_color_make(r, g, b);
    return t;
}

// Indexed by (int)WaitLevel. Defaults come from waitdefaults.h (the single
// source shared with RuntimeConfig::waitColors and StatusLed).
static WaitTheme WAIT_THEMES[5] = {
    themeFromColor(WAIT_COLOR_DEFAULTS[0]), themeFromColor(WAIT_COLOR_DEFAULTS[1]),
    themeFromColor(WAIT_COLOR_DEFAULTS[2]), themeFromColor(WAIT_COLOR_DEFAULTS[3]),
    themeFromColor(WAIT_COLOR_DEFAULTS[4]),
};
static uint8_t WAIT_TH1 = WAIT_TH_DEFAULTS[0], WAIT_TH2 = WAIT_TH_DEFAULTS[1],
               WAIT_TH3 = WAIT_TH_DEFAULTS[2];

// Raw configured wait colours (0xRRGGBB), retained so the wait-panel
// backgrounds can be re-derived whenever EITHER the colours (setWaitConfig) or
// the palette lightness (applyPalette) changes.
static uint32_t s_waitColors[5] = {
    WAIT_COLOR_DEFAULTS[0], WAIT_COLOR_DEFAULTS[1], WAIT_COLOR_DEFAULTS[2],
    WAIT_COLOR_DEFAULTS[3], WAIT_COLOR_DEFAULTS[4] };
static void rebuildWaitThemes() {
    for (int i = 0; i < 5; i++) WAIT_THEMES[i] = themeFromColor(s_waitColors[i]);
}

// Legacy names — the T_* themes are used all over this file.
#define T_GREEN  (WAIT_THEMES[(int)WaitLevel::Green])
#define T_AMBER  (WAIT_THEMES[(int)WaitLevel::Amber])
#define T_ORANGE (WAIT_THEMES[(int)WaitLevel::Orange])
#define T_RED    (WAIT_THEMES[(int)WaitLevel::Red])
#define T_TEAL   (WAIT_THEMES[(int)WaitLevel::Closed])

static const WaitTheme& pickTheme(int waitTime, bool isOpen) {
    return WAIT_THEMES[(int)pickWaitLevel(waitTime, isOpen,
                                          WAIT_TH1, WAIT_TH2, WAIT_TH3)];
}

// ---------------------------------------------------------------------------
// Layout constants  (landscape 320×172)
//
//  Y=0   Header 36px  (indigo→navy gradient)
//  Y=36  Gold separator 1px
//  Y=37  Progress bar 3px  (themed accent fill)
//  Y=40  Ride panel 48px   (row 1: name + index, row 2: land + NEXT hint)
//  Y=88  Wait panel 84px   (big number + trend arrow + sub-label)
// ---------------------------------------------------------------------------
static constexpr int SCR_W = LCD_W;  // 320
static constexpr int SCR_H = LCD_H;  // 172

static constexpr int HDR_H      = 36;
static constexpr int HDR_PAD_X  = 10;
static constexpr int HDR_PAD_Y  = 9;
static constexpr int PARK_LBL_W = 220;   // leaves ~78px for HH:MM:SS clock

static constexpr int GOLD_Y = HDR_H;
static constexpr int GOLD_H = 1;

static constexpr int PROG_Y = GOLD_Y + GOLD_H;  // 37
static constexpr int PROG_H = 3;

static constexpr int RIDE_Y        = PROG_Y + PROG_H;  // 40
static constexpr int RIDE_H        = 48;
static constexpr int RIDE_PAD_Y    = 4;               // row 1 top
static constexpr int RIDE_ACCENT_W = 4;
static constexpr int RIDE_NAME_X   = RIDE_ACCENT_W + 6;  // 10
static constexpr int RIDE_NAME_W   = 264;
static constexpr int RIDE_NAME_H   = 24;
static constexpr int RIDE_IDX_X    = 264;
static constexpr int RIDE_IDX_W    = SCR_W - RIDE_IDX_X - 4;  // 52 ("* 12/12")
static constexpr int RIDE_ROW2_Y   = 29;
static constexpr int RIDE_ROW2_H   = 16;
static constexpr int RIDE_LAND_W   = SCR_W - RIDE_NAME_X - 10;   // full width

static constexpr int WAIT_Y     = RIDE_Y + RIDE_H;  // 88
static constexpr int WAIT_H     = SCR_H - WAIT_Y;   // 84
static constexpr int WAIT_BDR_H = 2;
static constexpr int WAIT_NUM_Y = 2;
static constexpr int WAIT_NUM_H = 56;
static constexpr int WAIT_SUB_Y = WAIT_NUM_Y + WAIT_NUM_H + 2;  // 60
static constexpr int WAIT_SUB_H = WAIT_H - WAIT_SUB_Y;          // 24
static constexpr int TREND_W    = 64;
static constexpr int TREND_X    = SCR_W - TREND_W - 6;
static constexpr int TREND_Y    = 10;
static constexpr int TREND_H    = 24;

// ---------------------------------------------------------------------------
// Widget helpers
// ---------------------------------------------------------------------------

static lv_obj_t* makeScreen() {
    lv_obj_t* scr = lv_obj_create(nullptr);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(scr, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(scr, 0, LV_PART_MAIN);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    return scr;
}

static lv_obj_t* makePanel(lv_obj_t* parent, int x, int y, int w, int h,
                            lv_color_t bg, int radius = 0) {
    lv_obj_t* obj = lv_obj_create(parent);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_radius(obj, radius, LV_PART_MAIN);
    lv_obj_set_style_pad_all(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(obj, bg, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

static lv_obj_t* makeLabel(lv_obj_t* parent, int x, int y, int w, int h,
                            const lv_font_t* font, lv_color_t color,
                            lv_text_align_t align = LV_TEXT_ALIGN_LEFT,
                            lv_label_long_mode_t longMode = LV_LABEL_LONG_CLIP) {
    lv_obj_t* lbl = lv_label_create(parent);
    lv_obj_set_pos(lbl, x, y);
    lv_obj_set_size(lbl, w, h);
    lv_obj_set_style_text_font(lbl, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl, color, LV_PART_MAIN);
    lv_obj_set_style_text_align(lbl, align, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(lbl, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(lbl, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(lbl, 0, LV_PART_MAIN);
    lv_label_set_long_mode(lbl, longMode);
    if (longMode == LV_LABEL_LONG_SCROLL_CIRCULAR || longMode == LV_LABEL_LONG_SCROLL)
        lv_obj_set_style_anim_speed(lbl, 26, LV_PART_MAIN);  // px/s
    lv_label_set_text(lbl, "");
    return lbl;
}

// ---------------------------------------------------------------------------
// begin()
// ---------------------------------------------------------------------------
DisplayController::DisplayController() {}

void DisplayController::begin() {
    LCD_Init();
    Lvgl_Init();
    _buildMainScreen();
    _buildStatusScreen();
    _buildPortalScreen();
    lv_scr_load(_scrStatus);
    lv_timer_handler();
}

// ---------------------------------------------------------------------------
// _buildMainScreen
// ---------------------------------------------------------------------------
void DisplayController::_buildMainScreen() {
    _scrMain = makeScreen();
    lv_obj_set_style_bg_color(_scrMain, C_BLACK, LV_PART_MAIN);

    // ── Header: solid palette colour (flat — no gradient banding on RGB565) ──
    lv_obj_t* hdr = makePanel(_scrMain, 0, 0, SCR_W, HDR_H, C_HDR_L);
    _pnlHdr = hdr;

    _lblPark = makeLabel(hdr, HDR_PAD_X, HDR_PAD_Y, PARK_LBL_W, HDR_H - HDR_PAD_Y,
                         &lv_font_montserrat_16, C_PARK_TXT,
                         LV_TEXT_ALIGN_LEFT, LV_LABEL_LONG_SCROLL_CIRCULAR);

    // Right side, two rows: small country (where the clock's time is from)
    // above the park-local time.
    _lblCountry = makeLabel(hdr, 0, 3, SCR_W - 8, 13,
                            &lv_font_montserrat_12, C_IDX_TXT,
                            LV_TEXT_ALIGN_RIGHT);

    _lblTime = makeLabel(hdr, 0, 17, SCR_W - 8, 17,
                         &lv_font_montserrat_16, C_TIME_TXT,
                         LV_TEXT_ALIGN_RIGHT);

    // ── Gold separator (1 px) — dims when data is stale ─────────────────────
    _objGoldSep = makePanel(_scrMain, 0, GOLD_Y, SCR_W, GOLD_H, C_GOLD);

    // ── Progress bar: themed accent colour fill ──────────────────────────────
    _barProgress = lv_bar_create(_scrMain);
    lv_obj_set_pos(_barProgress, 0, PROG_Y);
    lv_obj_set_size(_barProgress, SCR_W, PROG_H);
    lv_obj_set_style_radius(_barProgress, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(_barProgress, 0, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(_barProgress, C_SEP, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(_barProgress, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(_barProgress, T_GREEN.accent, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(_barProgress, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_border_width(_barProgress, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(_barProgress, 0, LV_PART_MAIN);
    lv_obj_set_style_anim_time(_barProgress, 250, LV_PART_MAIN);  // smooth fill on ride change
    lv_bar_set_range(_barProgress, 0, 100);
    lv_bar_set_value(_barProgress, 0, LV_ANIM_OFF);

    // ── Ride panel: two rows ─────────────────────────────────────────────────
    lv_obj_t* ridePanel = makePanel(_scrMain, 0, RIDE_Y, SCR_W, RIDE_H, C_RIDE_BG);
    _pnlRide = ridePanel;

    // Left colour stripe — updates with wait theme
    _objRideAccent = makePanel(ridePanel, 0, 0, RIDE_ACCENT_W, RIDE_H, T_GREEN.accent);

    _lblRideName = makeLabel(ridePanel, RIDE_NAME_X, RIDE_PAD_Y, RIDE_NAME_W,
                             RIDE_NAME_H,
                             &lv_font_montserrat_20, C_RIDE_TXT,
                             LV_TEXT_ALIGN_LEFT, LV_LABEL_LONG_SCROLL_CIRCULAR);

    _lblRideIdx = makeLabel(ridePanel, RIDE_IDX_X, RIDE_PAD_Y + 4, RIDE_IDX_W,
                            RIDE_NAME_H - 4,
                            &lv_font_montserrat_14, C_IDX_TXT,
                            LV_TEXT_ALIGN_RIGHT);

    // Row 2: themed-land name ("Frontierland", "Tomorrowland", ...)
    _lblLand = makeLabel(ridePanel, RIDE_NAME_X, RIDE_ROW2_Y, RIDE_LAND_W,
                         RIDE_ROW2_H,
                         &lv_font_montserrat_12, C_IDX_TXT,
                         LV_TEXT_ALIGN_LEFT);

    // ── Wait panel: solid themed background + accent border + number ─────────
    _objWaitPanel = makePanel(_scrMain, 0, WAIT_Y, SCR_W, WAIT_H, T_GREEN.bgBot);

    _objWaitBorder = makePanel(_objWaitPanel, 0, 0, SCR_W, WAIT_BDR_H, T_GREEN.accent);

    _lblWaitNum = makeLabel(_objWaitPanel, 0, WAIT_NUM_Y, SCR_W, WAIT_NUM_H,
                            &lv_font_montserrat_48, T_GREEN.accent,
                            LV_TEXT_ALIGN_CENTER);

    // Trend arrow + delta ("▲ 10") — top-right of the wait panel
    _lblTrend = makeLabel(_objWaitPanel, TREND_X, TREND_Y, TREND_W, TREND_H,
                          &lv_font_montserrat_20, C_BODY_TXT,
                          LV_TEXT_ALIGN_RIGHT);

    _lblWaitSub = makeLabel(_objWaitPanel, 0, WAIT_SUB_Y, SCR_W, WAIT_SUB_H,
                            &lv_font_montserrat_14, C_BODY_TXT,
                            LV_TEXT_ALIGN_CENTER);
    // Tracked-out caps give the sub-label a clean "unit" look under the number
    lv_obj_set_style_text_letter_space(_lblWaitSub, 2, LV_PART_MAIN);
}

// ---------------------------------------------------------------------------
// _buildStatusScreen
// ---------------------------------------------------------------------------
void DisplayController::_buildStatusScreen() {
    _scrStatus = makeScreen();
    // Solid palette colour (flat — avoids RGB565 gradient banding)
    lv_obj_set_style_bg_color(_scrStatus, C_STAT_BG, LV_PART_MAIN);

    _lblStTitle = makeLabel(_scrStatus, 0, 20, SCR_W, 28,
                            &lv_font_montserrat_20, C_WHITE,
                            LV_TEXT_ALIGN_CENTER);

    // Accent separator under title
    _sepStatus = makePanel(_scrStatus, 30, 52, SCR_W - 60, 2, C_GOLD);

    _lblStSub = makeLabel(_scrStatus, 12, 60, SCR_W - 24, 22,
                          &lv_font_montserrat_14, C_TIME_TXT,
                          LV_TEXT_ALIGN_CENTER);

    _lblStBody = makeLabel(_scrStatus, 12, 86, SCR_W - 24, 46,
                           &lv_font_montserrat_14, C_BODY_TXT,
                           LV_TEXT_ALIGN_CENTER);

    // Bottom accent panel (solid)
    _objStBottom = makePanel(_scrStatus, 0, 140, SCR_W, 32, C_PANEL_BG);
    _lineStBottom = makePanel(_scrStatus, 0, 140, SCR_W, 1, C_GOLD);  // top border

    _lblStExtra = makeLabel(_objStBottom, 0, 7, SCR_W, 22,
                            &lv_font_montserrat_16, C_PARK_TXT,
                            LV_TEXT_ALIGN_CENTER);
}

// ---------------------------------------------------------------------------
// _buildPortalScreen — Wi-Fi setup with a scannable QR code
//
//  Left column  : short instructions
//  Right         : QR code on a white quiet-zone panel (phone camera auto-joins)
//  Bottom bar    : SSID / password fallback for manual entry
// ---------------------------------------------------------------------------
void DisplayController::_buildPortalScreen() {
    _scrPortal = makeScreen();
    lv_obj_set_style_bg_color(_scrPortal, C_STAT_BG, LV_PART_MAIN);

    // Left column — instructions
    lv_obj_t* title = makeLabel(_scrPortal, 12, 14, 168, 26,
                                &lv_font_montserrat_20, C_PARK_TXT,
                                LV_TEXT_ALIGN_LEFT);
    lv_label_set_text(title, "Wi-Fi Setup");
    _lblPortalTitle = title;

    _sepPortal = makePanel(_scrPortal, 12, 44, 140, 2, C_GOLD);

    lv_obj_t* body = makeLabel(_scrPortal, 12, 54, 170, 86,
                               &lv_font_montserrat_14, C_RIDE_TXT,
                               LV_TEXT_ALIGN_LEFT, LV_LABEL_LONG_WRAP);
    _lblPortalBody = body;
    lv_label_set_text(body,
        "Scan with your phone\ncamera to join, then\nopen the page shown\n(or 192.168.4.1).");

    // Right — white quiet-zone panel behind the QR code (aids scanning)
    makePanel(_scrPortal, 180, 12, 132, 132, C_WHITE);
    _qrPortal = lv_qrcode_create(_scrPortal, 116, C_BLACK, C_WHITE);
    lv_obj_set_pos(_qrPortal, 188, 20);
    lv_obj_set_style_border_width(_qrPortal, 0, LV_PART_MAIN);

    // Bottom bar — manual-entry fallback (SSID / password)
    lv_obj_t* bar = makePanel(_scrPortal, 0, 148, SCR_W, SCR_H - 148, C_PANEL_BG);
    _pnlPortalBar  = bar;
    _linePortalBar = makePanel(_scrPortal, 0, 148, SCR_W, 1, C_GOLD);
    _lblPortalNet = makeLabel(bar, 0, 3, SCR_W, 20,
                              &lv_font_montserrat_14, T_TEAL.accent,
                              LV_TEXT_ALIGN_CENTER);
    lv_label_set_text(_lblPortalNet, "");
}

// ---------------------------------------------------------------------------
// Screen switching — fade transitions
// ---------------------------------------------------------------------------
void DisplayController::_loadMain() {
    if (lv_scr_act() != _scrMain)
        lv_scr_load_anim(_scrMain, LV_SCR_LOAD_ANIM_FADE_ON, 220, 0, false);
}

void DisplayController::_loadStatus() {
    if (lv_scr_act() != _scrStatus)
        lv_scr_load_anim(_scrStatus, LV_SCR_LOAD_ANIM_FADE_ON, 220, 0, false);
}

void DisplayController::_loadPortal() {
    if (lv_scr_act() != _scrPortal)
        lv_scr_load_anim(_scrPortal, LV_SCR_LOAD_ANIM_FADE_ON, 220, 0, false);
}

// ---------------------------------------------------------------------------
// _applyWaitWidgets
// ---------------------------------------------------------------------------
void DisplayController::_applyWaitWidgets(const RideInfo& ride) {
    const WaitTheme& th = pickTheme(ride.waitTime, ride.isOpen);

    // Solid themed background on wait panel
    lv_obj_set_style_bg_color(_objWaitPanel, th.bgBot, LV_PART_MAIN);

    // Accent elements: border, number text, progress bar, left stripe
    lv_obj_set_style_bg_color(_objWaitBorder, th.accent, LV_PART_MAIN);
    lv_obj_set_style_text_color(_lblWaitNum, th.accent, LV_PART_MAIN);
    lv_obj_set_style_bg_color(_barProgress, th.accent, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(_objRideAccent, th.accent, LV_PART_MAIN);

    // Wait number / text
    lv_obj_set_style_text_font(_lblWaitNum, &lv_font_montserrat_48, LV_PART_MAIN);
    if (!ride.isOpen) {
        lv_label_set_text(_lblWaitNum, "CLOSED");
        lv_label_set_text(_lblWaitSub, "NOT OPERATING");
    } else if (ride.waitTime < 0) {
        lv_label_set_text(_lblWaitNum, "---");
        lv_label_set_text(_lblWaitSub, "NO DATA");
    } else {
        char num[8];
        snprintf(num, sizeof(num), "%d", ride.waitTime);
        lv_label_set_text(_lblWaitNum, num);
        lv_label_set_text(_lblWaitSub, "MIN WAIT");  // sidesteps the singular/plural issue
    }

    // Trend arrow: red rising (wait getting worse), green falling. Hidden
    // when flat or closed. Arrow glyphs ship in every Montserrat font.
    if (!ride.isOpen || ride.trend == 0) {
        lv_label_set_text(_lblTrend, "");
    } else {
        int deltaAbs = ride.trendDelta < 0 ? -ride.trendDelta : ride.trendDelta;
        char buf[16];
        snprintf(buf, sizeof(buf), "%s %d",
                 ride.trend > 0 ? LV_SYMBOL_UP : LV_SYMBOL_DOWN, deltaAbs);
        lv_label_set_text(_lblTrend, buf);
        lv_obj_set_style_text_color(_lblTrend,
            ride.trend > 0 ? T_RED.accent : T_GREEN.accent, LV_PART_MAIN);
    }

    _lastWaitTime = ride.waitTime;
    _lastIsOpen   = ride.isOpen;
    _lastTrend    = ride.trend;
}

// ---------------------------------------------------------------------------
// _setRideName
// ---------------------------------------------------------------------------
void DisplayController::_setRideName(const String& name) {
    lv_label_set_text(_lblRideName, name.c_str());
    _lastRideName = name;
}

void DisplayController::_setLand(const String& land) {
    lv_label_set_text(_lblLand, land.c_str());   // "" collapses the row
    _lastLand = land;
}

// ---------------------------------------------------------------------------
// Public API — main screen
// ---------------------------------------------------------------------------

void DisplayController::drawBackground() {
    _loadMain();
}

void DisplayController::drawParkName(const String& parkName, bool force) {
    if (!force && parkName == _lastParkName) {
        lv_label_set_text(_lblTime, getLocalTimeString().c_str());
        return;
    }
    _lastParkName = parkName;
    lv_label_set_text(_lblPark, parkName.c_str());
    lv_label_set_text(_lblTime, getLocalTimeString().c_str());
}

void DisplayController::setRideCount(int count) {
    _rideCount = count;
}

void DisplayController::setParkCountry(const String& country) {
    if (country == _parkCountry) return;
    _parkCountry = country;
    lv_label_set_text(_lblCountry, country.c_str());   // "" collapses the row
}

void DisplayController::drawProgressBar(int currentIdx, int totalCount, bool favorite) {
    if (totalCount <= 0) {
        lv_bar_set_value(_barProgress, 0, LV_ANIM_OFF);
        lv_label_set_text(_lblRideIdx, "");
        return;
    }
    int pct = (currentIdx + 1) * 100 / totalCount;
    lv_bar_set_value(_barProgress, pct, LV_ANIM_ON);
    char buf[12];
    snprintf(buf, sizeof(buf), "%s%d/%d", favorite ? "* " : "",
             currentIdx + 1, totalCount);
    lv_label_set_text(_lblRideIdx, buf);
    // Gold star marker when the current ride is a favorite
    lv_obj_set_style_text_color(_lblRideIdx, favorite ? C_PARK_TXT : C_IDX_TXT,
                                LV_PART_MAIN);
    _lastFavorite = favorite;
}

void DisplayController::displayRide(const RideInfo& ride, int rideIdx) {
    _loadMain();
    _setRideName(ride.name);
    _setLand(ride.land);
    if (_rideCount > 0) drawProgressBar(rideIdx, _rideCount, ride.favorite);
    _applyWaitWidgets(ride);
    _lastRideIdx = rideIdx;
}

void DisplayController::updateRideIfChanged(const RideInfo& ride, int rideIdx) {
    const bool nameChanged = (ride.name != _lastRideName);
    const bool landChanged = (ride.land != _lastLand);
    const bool idxChanged  = (rideIdx  != _lastRideIdx ||
                              ride.favorite != _lastFavorite);
    const bool waitChanged = (ride.waitTime != _lastWaitTime ||
                              ride.isOpen   != _lastIsOpen   ||
                              ride.trend    != _lastTrend);

    if (!nameChanged && !landChanged && !idxChanged && !waitChanged) return;

    if (nameChanged) _setRideName(ride.name);
    if (landChanged) _setLand(ride.land);
    if (idxChanged && _rideCount > 0) {
        drawProgressBar(rideIdx, _rideCount, ride.favorite);
        _lastRideIdx = rideIdx;
    }
    if (waitChanged) _applyWaitWidgets(ride);
}

void DisplayController::redrawWaitTime(const RideInfo& ride) {
    _applyWaitWidgets(ride);
}

void DisplayController::setDataFreshness(int ageMinutes) {
    if (_objGoldSep == nullptr) return;
    _lastAgeMin = ageMinutes;
    lv_color_t c = (ageMinutes < 5)  ? C_GOLD :
                   (ageMinutes < 15) ? C_GOLD_DIM : C_GOLD_OLD;
    lv_obj_set_style_bg_color(_objGoldSep, c, LV_PART_MAIN);
}

// ---------------------------------------------------------------------------
// applyPalette — switch the UI "chrome" palette and restyle every widget
// that was coloured at build time. Colours applied at call time (message
// text, wait themes) pick up the new palette on their next draw; the wait
// themes are palette-independent by design.
// ---------------------------------------------------------------------------
using PaletteColorFn = void (*)(lv_obj_t*, lv_color_t, lv_style_selector_t);

struct PaletteBinding {
    // A true pointer-to-member (not a raw lv_obj_t**) so the table is safe
    // as a function-local `static const` regardless of which
    // DisplayController instance calls applyPalette() first — a raw member
    // address would be captured from whatever `this` happened to be on the
    // static's one-time initialization, silently aliasing every other
    // instance thereafter.
    lv_obj_t* DisplayController::* widget;
    lv_color_t                     UiPalette::* field;
    PaletteColorFn                 setter;
};

void DisplayController::applyPalette(uint8_t paletteId) {
    if (paletteId == CUSTOM_PALETTE_INDEX)   PAL = &s_customPalette;
    else if (paletteId < UI_PALETTE_COUNT)   PAL = &PALETTES[paletteId];
    else                                     PAL = &PALETTES[0];

    // The wait-panel background is a palette colour (see themeFromColor), so a
    // palette change must re-derive the wait themes even without a following
    // setWaitConfig() call.
    rebuildWaitThemes();

    // Every palette-coloured widget that isn't special-cased below (the ride
    // index has a favorite-conditional colour; the freshness separator is
    // re-derived via setDataFreshness). Adding a new plain palette-coloured
    // widget only needs a row here, not a hand-written set_style_* call.
    static const PaletteBinding kBindings[] = {
        { &DisplayController::_pnlHdr,        &UiPalette::hdrBg,    lv_obj_set_style_bg_color   },
        { &DisplayController::_lblPark,       &UiPalette::parkTxt,  lv_obj_set_style_text_color },
        { &DisplayController::_lblCountry,    &UiPalette::idxTxt,   lv_obj_set_style_text_color },
        { &DisplayController::_lblTime,       &UiPalette::timeTxt,  lv_obj_set_style_text_color },
        { &DisplayController::_barProgress,   &UiPalette::track,    lv_obj_set_style_bg_color   },
        { &DisplayController::_pnlRide,       &UiPalette::rideBg,   lv_obj_set_style_bg_color   },
        { &DisplayController::_lblRideName,   &UiPalette::rideTxt,  lv_obj_set_style_text_color },
        { &DisplayController::_lblLand,       &UiPalette::idxTxt,   lv_obj_set_style_text_color },
        { &DisplayController::_lblWaitSub,    &UiPalette::bodyTxt,  lv_obj_set_style_text_color },
        // Status screen (title/sub colours are per-message; the next show*()
        // call re-applies them from the new palette)
        { &DisplayController::_scrStatus,     &UiPalette::statusBg, lv_obj_set_style_bg_color   },
        { &DisplayController::_sepStatus,     &UiPalette::accent,   lv_obj_set_style_bg_color   },
        { &DisplayController::_lblStBody,     &UiPalette::bodyTxt,  lv_obj_set_style_text_color },
        { &DisplayController::_objStBottom,   &UiPalette::panelBg,  lv_obj_set_style_bg_color   },
        { &DisplayController::_lineStBottom,  &UiPalette::accent,   lv_obj_set_style_bg_color   },
        { &DisplayController::_lblStExtra,    &UiPalette::parkTxt,  lv_obj_set_style_text_color },
        // Portal screen
        { &DisplayController::_scrPortal,     &UiPalette::statusBg, lv_obj_set_style_bg_color   },
        { &DisplayController::_lblPortalTitle,&UiPalette::parkTxt,  lv_obj_set_style_text_color },
        { &DisplayController::_sepPortal,     &UiPalette::accent,   lv_obj_set_style_bg_color   },
        { &DisplayController::_lblPortalBody, &UiPalette::rideTxt,  lv_obj_set_style_text_color },
        { &DisplayController::_pnlPortalBar,  &UiPalette::panelBg,  lv_obj_set_style_bg_color   },
        { &DisplayController::_linePortalBar, &UiPalette::accent,   lv_obj_set_style_bg_color   },
    };
    for (const PaletteBinding& b : kBindings) {
        b.setter(this->*b.widget, PAL->*(b.field), LV_PART_MAIN);
    }

    // Special cases that aren't a plain palette-field lookup.
    lv_obj_set_style_text_color(_lblRideIdx,
        _lastFavorite ? C_PARK_TXT : C_IDX_TXT, LV_PART_MAIN);
    setDataFreshness(_lastAgeMin);

    lv_obj_invalidate(lv_scr_act());
}

// ---------------------------------------------------------------------------
// setWaitConfig — user-configured wait thresholds and level colours. The
// caller repaints (restartCycle / next draw), so no restyle happens here:
// every wait-theme colour is looked up from WAIT_THEMES at draw time.
// ---------------------------------------------------------------------------
void DisplayController::setWaitConfig(uint8_t th1, uint8_t th2, uint8_t th3,
                                      const uint32_t colors[5]) {
    WAIT_TH1 = th1;
    WAIT_TH2 = th2;
    WAIT_TH3 = th3;
    for (int i = 0; i < 5; i++) s_waitColors[i] = colors[i];
    rebuildWaitThemes();
}

// ---------------------------------------------------------------------------
// setCustomPalette — build the "Custom" palette from 3 user-picked colours,
// deriving the remaining fields (text shades, dims, backgrounds) so the result
// stays legible whatever colours the user chose. The caller repaints.
// ---------------------------------------------------------------------------
void DisplayController::setCustomPalette(uint32_t hdrRgb, uint32_t accRgb,
                                         uint32_t pnlRgb) {
    auto C = [](uint32_t c) {
        return lv_color_make((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF);
    };
    lv_color_t hdr = C(hdrRgb), acc = C(accRgb), pnl = C(pnlRgb);
    const lv_color_t LIGHT = LV_COLOR_MAKE(0xF0, 0xF2, 0xFA);
    const lv_color_t DARK  = LV_COLOR_MAKE(0x18, 0x1E, 0x28);
    // High-contrast text per background (light bg -> dark ink, and vice versa).
    lv_color_t txtHdr = (lv_color_brightness(hdr) > 140) ? DARK : LIGHT;
    lv_color_t txtPnl = (lv_color_brightness(pnl) > 140) ? DARK : LIGHT;

    UiPalette& p = s_customPalette;
    p.hdrBg     = hdr;
    p.accent    = acc;
    p.accentDim = lv_color_mix(acc, hdr, 110);       // 5-14 min stale separator
    p.accentOld = lv_color_mix(acc, DARK, 120);      // 15+ min stale separator
    p.track     = lv_color_mix(hdr, txtHdr, 40);     // progress-bar track
    p.parkTxt   = txtHdr;                            // park name
    p.timeTxt   = lv_color_mix(txtHdr, hdr, 210);    // clock (slightly muted)
    p.idxTxt    = lv_color_mix(txtHdr, hdr, 150);    // index/land/country (muted)
    p.rideTxt   = txtPnl;                            // ride name
    p.bodyTxt   = txtPnl;                            // body/status + wait sub-label
    p.rideBg    = pnl;
    p.panelBg   = lv_color_mix(pnl, txtPnl, 30);     // status/portal bottom bar
    p.statusBg  = pnl;
}

// ---------------------------------------------------------------------------
// Public API — status screens
// ---------------------------------------------------------------------------

void DisplayController::showNoData(NoDataReason reason) {
    _loadStatus();

    switch (reason) {
    case NoDataReason::NO_PARKS:
        lv_obj_set_style_text_color(_lblStTitle, C_PARK_TXT, LV_PART_MAIN);
        lv_label_set_text(_lblStTitle, LV_SYMBOL_SETTINGS "  No Parks Set");
        lv_obj_set_style_text_color(_lblStSub, C_BODY_TXT, LV_PART_MAIN);
        lv_label_set_text(_lblStSub, "Configure parks to get started");
        lv_label_set_text(_lblStBody, "Open your browser and visit:");
        lv_label_set_text(_lblStExtra, "queuewatch.local");
        break;

    case NoDataReason::NO_RIDES:
        lv_obj_set_style_text_color(_lblStTitle, C_PARK_TXT, LV_PART_MAIN);
        lv_label_set_text(_lblStTitle, LV_SYMBOL_LIST "  No Rides Selected");
        lv_obj_set_style_text_color(_lblStSub, C_BODY_TXT, LV_PART_MAIN);
        lv_label_set_text(_lblStSub, "Your ride filter hides every ride");
        lv_label_set_text(_lblStBody, "Adjust the filter in your browser:");
        lv_label_set_text(_lblStExtra, "queuewatch.local");
        break;

    case NoDataReason::WIFI_LOST:
        lv_obj_set_style_text_color(_lblStTitle, T_RED.accent, LV_PART_MAIN);
        lv_label_set_text(_lblStTitle, LV_SYMBOL_WARNING "  Offline");
        lv_obj_set_style_text_color(_lblStSub, T_RED.accent, LV_PART_MAIN);
        lv_label_set_text(_lblStSub, "WiFi connection lost");
        lv_label_set_text(_lblStBody, "Reconnecting automatically...");
        lv_label_set_text(_lblStExtra, "");
        break;

    case NoDataReason::WIFI_TROUBLE:
        lv_obj_set_style_text_color(_lblStTitle, T_AMBER.accent, LV_PART_MAIN);
        lv_label_set_text(_lblStTitle, LV_SYMBOL_WIFI "  WiFi Not Found");
        lv_obj_set_style_text_color(_lblStSub, T_AMBER.accent, LV_PART_MAIN);
        lv_label_set_text(_lblStSub, "Still trying to connect...");
        lv_label_set_text(_lblStBody,
            "Check the network is on and in range.\n"
            "Hold BOOT 20 s to erase WiFi and set up again.");
        lv_label_set_text(_lblStExtra, "");
        break;

    case NoDataReason::FETCH_FAILED:
        lv_obj_set_style_text_color(_lblStTitle, T_ORANGE.accent, LV_PART_MAIN);
        lv_label_set_text(_lblStTitle, LV_SYMBOL_REFRESH "  No Ride Data");
        lv_obj_set_style_text_color(_lblStSub, C_BODY_TXT, LV_PART_MAIN);
        lv_label_set_text(_lblStSub, "Couldn't reach queue-times.com");
        lv_label_set_text(_lblStBody, "Retrying shortly...");
        lv_label_set_text(_lblStExtra, "");
        break;
    }
}

void DisplayController::showFactoryResetWarning() {
    _loadStatus();
    lv_obj_set_style_text_color(_lblStTitle, T_RED.accent, LV_PART_MAIN);
    lv_label_set_text(_lblStTitle, LV_SYMBOL_WARNING "  Factory Reset?");
    lv_obj_set_style_text_color(_lblStSub, T_RED.accent, LV_PART_MAIN);
    lv_label_set_text(_lblStSub, "Are you sure you want to do this?");
    lv_label_set_text(_lblStBody,
        "Keep holding to erase WiFi + all settings.\nRelease the button to cancel.");
    lv_label_set_text(_lblStExtra, "");
}

// The caller restarts the device right after this returns, so the screen is
// loaded directly and painted NOW — a fade animation would never finish.
void DisplayController::showFactoryResetting() {
    lv_obj_set_style_text_color(_lblStTitle, T_RED.accent, LV_PART_MAIN);
    lv_label_set_text(_lblStTitle, LV_SYMBOL_TRASH "  Resetting...");
    lv_obj_set_style_text_color(_lblStSub, C_BODY_TXT, LV_PART_MAIN);
    lv_label_set_text(_lblStSub, "Erasing WiFi and all settings");
    lv_label_set_text(_lblStBody, "The device will restart into WiFi setup.");
    lv_label_set_text(_lblStExtra, "");
    if (lv_scr_act() != _scrStatus) lv_scr_load(_scrStatus);
    lv_refr_now(NULL);
}

void DisplayController::showOtaDownloading(uint8_t progressPct) {
    lv_obj_set_style_text_color(_lblStTitle, C_GOLD, LV_PART_MAIN);
    lv_label_set_text(_lblStTitle, LV_SYMBOL_DOWNLOAD "  Downloading Update");
    lv_obj_set_style_text_color(_lblStSub, C_BODY_TXT, LV_PART_MAIN);
    char pctBuf[8];
    snprintf(pctBuf, sizeof(pctBuf), "%u%%", (unsigned)progressPct);
    lv_label_set_text(_lblStSub, pctBuf);
    lv_label_set_text(_lblStBody, "Please don't power off the device.");
    lv_label_set_text(_lblStExtra, "");
    if (lv_scr_act() != _scrStatus) lv_scr_load(_scrStatus);
    lv_refr_now(NULL);
}

// The caller restarts the device right after this returns, so the screen is
// loaded directly and painted NOW — a fade animation would never finish
// (same reasoning as showFactoryResetting()).
void DisplayController::showOtaInstalling() {
    lv_obj_set_style_text_color(_lblStTitle, C_GOLD, LV_PART_MAIN);
    lv_label_set_text(_lblStTitle, LV_SYMBOL_OK "  Installing Update");
    lv_obj_set_style_text_color(_lblStSub, C_BODY_TXT, LV_PART_MAIN);
    lv_label_set_text(_lblStSub, "Verifying and applying...");
    lv_label_set_text(_lblStBody, "The device will restart shortly.");
    lv_label_set_text(_lblStExtra, "");
    if (lv_scr_act() != _scrStatus) lv_scr_load(_scrStatus);
    lv_refr_now(NULL);
}

void DisplayController::showClosedPark(const String& parkName) {
    _loadMain();
    drawParkName(parkName, true);
    _setRideName("All rides closed");
    _setLand("");
    lv_bar_set_value(_barProgress, 0, LV_ANIM_OFF);
    lv_label_set_text(_lblRideIdx, "");

    // pickTheme() (inside _applyWaitWidgets) forces the teal Closed theme
    // whenever isOpen is false, regardless of waitTime — a default
    // RideInfo (isOpen=false) reuses the same theme/number/trend logic a
    // single closed ride uses, instead of re-deriving it here.
    _applyWaitWidgets(RideInfo());
    lv_label_set_text(_lblWaitSub, "PARK IS CLOSED TODAY");  // more specific than a single ride's "NOT OPERATING"
}

void DisplayController::showCaptivePortalInfo(const char* apName, const char* apPass) {
    // Encode a standard Wi-Fi join string so a phone camera can auto-connect
    // to the setup access point:  WIFI:S:<ssid>;T:WPA;P:<pass>;;
    // (The AP name/password are fixed constants with no reserved characters,
    //  so no escaping is required here.)
    char wifi[128];
    snprintf(wifi, sizeof(wifi), "WIFI:S:%s;T:WPA;P:%s;;", apName, apPass);
    if (_qrPortal) lv_qrcode_update(_qrPortal, wifi, strlen(wifi));

    if (_lblPortalNet) {
        char net[96];
        snprintf(net, sizeof(net), "%s   /   %s", apName, apPass);
        lv_label_set_text(_lblPortalNet, net);
    }
    // The captive portal runs a blocking loop (WiFiManager::runCaptivePortal)
    // that never calls lv_timer_handler(), so a fade animation would never
    // advance and the screen would freeze on the previous screen. Load the
    // portal screen directly and paint it NOW, before that loop blocks.
    if (lv_scr_act() != _scrPortal) lv_scr_load(_scrPortal);
    lv_refr_now(NULL);
}

void DisplayController::showStartupInfo(const String& ipAddress) {
    _loadStatus();
    lv_obj_set_style_text_color(_lblStTitle, T_GREEN.accent, LV_PART_MAIN);
    lv_label_set_text(_lblStTitle, LV_SYMBOL_OK "  Connected!");

    lv_obj_set_style_text_color(_lblStSub, C_PARK_TXT, LV_PART_MAIN);
    lv_label_set_text(_lblStSub, "QueueWatch is ready");

    lv_label_set_text(_lblStBody, "Configure parks in your browser:");
    lv_label_set_text(_lblStExtra, ipAddress.c_str());
}

void DisplayController::showConnectingScreen(int dotCount) {
    _loadStatus();
    lv_obj_set_style_text_color(_lblStTitle, C_PARK_TXT, LV_PART_MAIN);
    lv_label_set_text(_lblStTitle, "QueueWatch");

    lv_obj_set_style_text_color(_lblStSub, C_BODY_TXT, LV_PART_MAIN);
    lv_label_set_text(_lblStSub, "Theme Park Wait Times");

    static const char* dots[] = { "", ".", "..", "..." };
    char body[48];
    snprintf(body, sizeof(body), LV_SYMBOL_WIFI "  Connecting to WiFi%s", dots[dotCount & 3]);
    lv_label_set_text(_lblStBody, body);
    lv_label_set_text(_lblStExtra, "");
}
