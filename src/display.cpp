#include "display.h"
#include "lcd_st7789.h"
#include "lvgl_driver.h"
#include "tzhelper.h"

// ---------------------------------------------------------------------------
// Colour palette — "Magic Night" theme
// ---------------------------------------------------------------------------
static const lv_color_t C_BLACK     = LV_COLOR_MAKE(0x00, 0x00, 0x00);
static const lv_color_t C_WHITE     = LV_COLOR_MAKE(0xFF, 0xFF, 0xFF);

// Header background (solid)
static const lv_color_t C_HDR_L    = LV_COLOR_MAKE(0x1E, 0x06, 0x42);  // deep indigo

// Gold separator — dims as data ages
static const lv_color_t C_GOLD     = LV_COLOR_MAKE(0xB8, 0x94, 0x1C);  // < 5 min (fresh)
static const lv_color_t C_GOLD_DIM = LV_COLOR_MAKE(0x70, 0x50, 0x08);  // 5–14 min (stale)
static const lv_color_t C_GOLD_OLD = LV_COLOR_MAKE(0x58, 0x18, 0x10);  // 15+ min (very stale)
static const lv_color_t C_SEP      = LV_COLOR_MAKE(0x18, 0x18, 0x38);  // progress bar track

// Text colours
static const lv_color_t C_PARK_TXT = LV_COLOR_MAKE(0xFF, 0xD4, 0x66);  // warm gold — park name
static const lv_color_t C_TIME_TXT = LV_COLOR_MAKE(0x70, 0x88, 0xD8);  // periwinkle — clock
static const lv_color_t C_IDX_TXT  = LV_COLOR_MAKE(0x60, 0x70, 0xA8);  // muted blue — "3/12"
static const lv_color_t C_RIDE_TXT = LV_COLOR_MAKE(0xEA, 0xEA, 0xFF);  // near-white — ride name
static const lv_color_t C_BODY_TXT = LV_COLOR_MAKE(0x88, 0x88, 0xB0);  // grey — body / status

// Panel backgrounds
static const lv_color_t C_RIDE_BG   = LV_COLOR_MAKE(0x0C, 0x0C, 0x20);
static const lv_color_t C_PANEL_BG  = LV_COLOR_MAKE(0x0A, 0x06, 0x1E);

// ---------------------------------------------------------------------------
// Wait-time themes: { bgTop, bgBot (gradient), accent }
// ---------------------------------------------------------------------------
struct WaitTheme { lv_color_t bgTop; lv_color_t bgBot; lv_color_t accent; };

static const WaitTheme T_GREEN  = { LV_COLOR_MAKE(0x03,0x12,0x07), LV_COLOR_MAKE(0x06,0x1E,0x0C), LV_COLOR_MAKE(0x00,0xE6,0x76) };
static const WaitTheme T_AMBER  = { LV_COLOR_MAKE(0x18,0x10,0x00), LV_COLOR_MAKE(0x24,0x1A,0x00), LV_COLOR_MAKE(0xFF,0xD6,0x00) };
static const WaitTheme T_ORANGE = { LV_COLOR_MAKE(0x18,0x07,0x00), LV_COLOR_MAKE(0x22,0x0D,0x00), LV_COLOR_MAKE(0xFF,0x70,0x43) };
static const WaitTheme T_RED    = { LV_COLOR_MAKE(0x18,0x00,0x05), LV_COLOR_MAKE(0x22,0x00,0x08), LV_COLOR_MAKE(0xFF,0x17,0x44) };
static const WaitTheme T_TEAL   = { LV_COLOR_MAKE(0x00,0x0E,0x18), LV_COLOR_MAKE(0x00,0x14,0x22), LV_COLOR_MAKE(0x18,0xFF,0xFF) };

static const WaitTheme& pickTheme(int waitTime, bool isOpen) {
    if (!isOpen)        return T_TEAL;
    if (waitTime <= 15) return T_GREEN;
    if (waitTime <= 30) return T_AMBER;
    if (waitTime <= 45) return T_ORANGE;
    return T_RED;
}

// ---------------------------------------------------------------------------
// Layout constants  (landscape 320×172)
//
//  Y=0   Header 36px  (indigo→navy gradient)
//  Y=36  Gold separator 1px
//  Y=37  Progress bar 3px  (themed accent fill)
//  Y=40  Ride panel 40px   (left accent stripe + ride name + index)
//  Y=80  Wait panel 92px   (gradient + big number + sub-label)
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
static constexpr int RIDE_H        = 40;
static constexpr int RIDE_PAD_Y    = 9;
static constexpr int RIDE_ACCENT_W = 4;
static constexpr int RIDE_NAME_X   = RIDE_ACCENT_W + 6;  // 10
static constexpr int RIDE_NAME_W   = 264;
static constexpr int RIDE_IDX_X    = 274;
static constexpr int RIDE_IDX_W    = SCR_W - RIDE_IDX_X - 4;  // 42

static constexpr int WAIT_Y     = RIDE_Y + RIDE_H;  // 80
static constexpr int WAIT_H     = SCR_H - WAIT_Y;   // 92
static constexpr int WAIT_BDR_H = 2;
static constexpr int WAIT_NUM_Y = 6;
static constexpr int WAIT_NUM_H = 60;
static constexpr int WAIT_SUB_Y = WAIT_NUM_Y + WAIT_NUM_H + 2;  // 68
static constexpr int WAIT_SUB_H = WAIT_H - WAIT_SUB_Y;          // 24

// ---------------------------------------------------------------------------
// Widget helpers
// ---------------------------------------------------------------------------

// Header clock text: small WiFi glyph + local time ("<wifi>  14:05").
// The glyph doubles as an at-a-glance "we're online" indicator — the main
// screen is only ever shown while connected.
static String clockText() {
    return String(LV_SYMBOL_WIFI "  ") + getLocalTimeString();
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
    _scrMain = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(_scrMain, C_BLACK, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(_scrMain, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(_scrMain, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(_scrMain, 0, LV_PART_MAIN);
    lv_obj_clear_flag(_scrMain, LV_OBJ_FLAG_SCROLLABLE);

    // ── Header: solid deep indigo (flat — no gradient banding on RGB565) ─────
    lv_obj_t* hdr = makePanel(_scrMain, 0, 0, SCR_W, HDR_H, C_HDR_L);

    _lblPark = makeLabel(hdr, HDR_PAD_X, HDR_PAD_Y, PARK_LBL_W, HDR_H - HDR_PAD_Y,
                         &lv_font_montserrat_16, C_PARK_TXT,
                         LV_TEXT_ALIGN_LEFT, LV_LABEL_LONG_SCROLL_CIRCULAR);

    _lblTime = makeLabel(hdr, 0, HDR_PAD_Y, SCR_W - 8, HDR_H - HDR_PAD_Y,
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

    // ── Ride panel ───────────────────────────────────────────────────────────
    lv_obj_t* ridePanel = makePanel(_scrMain, 0, RIDE_Y, SCR_W, RIDE_H, C_RIDE_BG);

    // Left colour stripe — updates with wait theme
    _objRideAccent = makePanel(ridePanel, 0, 0, RIDE_ACCENT_W, RIDE_H, T_GREEN.accent);

    _lblRideName = makeLabel(ridePanel, RIDE_NAME_X, RIDE_PAD_Y, RIDE_NAME_W,
                             RIDE_H - RIDE_PAD_Y,
                             &lv_font_montserrat_20, C_RIDE_TXT,
                             LV_TEXT_ALIGN_LEFT, LV_LABEL_LONG_SCROLL_CIRCULAR);

    _lblRideIdx = makeLabel(ridePanel, RIDE_IDX_X, RIDE_PAD_Y + 2, RIDE_IDX_W,
                            RIDE_H - RIDE_PAD_Y - 2,
                            &lv_font_montserrat_14, C_IDX_TXT,
                            LV_TEXT_ALIGN_RIGHT);

    // ── Wait panel: solid themed background + accent border + number ─────────
    _objWaitPanel = makePanel(_scrMain, 0, WAIT_Y, SCR_W, WAIT_H, T_GREEN.bgBot);

    _objWaitBorder = makePanel(_objWaitPanel, 0, 0, SCR_W, WAIT_BDR_H, T_GREEN.accent);

    _lblWaitNum = makeLabel(_objWaitPanel, 0, WAIT_NUM_Y, SCR_W, WAIT_NUM_H,
                            &lv_font_montserrat_48, T_GREEN.accent,
                            LV_TEXT_ALIGN_CENTER);

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
    _scrStatus = lv_obj_create(nullptr);
    // Solid deep purple (flat — avoids RGB565 gradient banding)
    lv_obj_set_style_bg_color(_scrStatus, LV_COLOR_MAKE(0x14, 0x00, 0x2A), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(_scrStatus, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(_scrStatus, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(_scrStatus, 0, LV_PART_MAIN);
    lv_obj_clear_flag(_scrStatus, LV_OBJ_FLAG_SCROLLABLE);

    _lblStTitle = makeLabel(_scrStatus, 0, 20, SCR_W, 28,
                            &lv_font_montserrat_20, C_WHITE,
                            LV_TEXT_ALIGN_CENTER);

    // Gold separator under title
    makePanel(_scrStatus, 30, 52, SCR_W - 60, 2, C_GOLD);

    _lblStSub = makeLabel(_scrStatus, 12, 60, SCR_W - 24, 22,
                          &lv_font_montserrat_14, C_TIME_TXT,
                          LV_TEXT_ALIGN_CENTER);

    _lblStBody = makeLabel(_scrStatus, 12, 86, SCR_W - 24, 46,
                           &lv_font_montserrat_14, C_BODY_TXT,
                           LV_TEXT_ALIGN_CENTER);

    // Bottom accent panel (solid)
    _objStBottom = makePanel(_scrStatus, 0, 140, SCR_W, 32, C_PANEL_BG);
    makePanel(_scrStatus, 0, 140, SCR_W, 1, C_GOLD);  // gold top border

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
    _scrPortal = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(_scrPortal, LV_COLOR_MAKE(0x14, 0x00, 0x2A), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(_scrPortal, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(_scrPortal, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(_scrPortal, 0, LV_PART_MAIN);
    lv_obj_clear_flag(_scrPortal, LV_OBJ_FLAG_SCROLLABLE);

    // Left column — instructions
    lv_obj_t* title = makeLabel(_scrPortal, 12, 14, 168, 26,
                                &lv_font_montserrat_20, C_PARK_TXT,
                                LV_TEXT_ALIGN_LEFT);
    lv_label_set_text(title, "Wi-Fi Setup");

    makePanel(_scrPortal, 12, 44, 140, 2, C_GOLD);

    lv_obj_t* body = makeLabel(_scrPortal, 12, 54, 170, 86,
                               &lv_font_montserrat_14, C_RIDE_TXT,
                               LV_TEXT_ALIGN_LEFT, LV_LABEL_LONG_WRAP);
    lv_label_set_text(body,
        "Scan with your phone\ncamera to join, then\nopen the page shown\n(or 192.168.4.1).");

    // Right — white quiet-zone panel behind the QR code (aids scanning)
    makePanel(_scrPortal, 180, 12, 132, 132, C_WHITE);
    _qrPortal = lv_qrcode_create(_scrPortal, 116, C_BLACK, C_WHITE);
    lv_obj_set_pos(_qrPortal, 188, 20);
    lv_obj_set_style_border_width(_qrPortal, 0, LV_PART_MAIN);

    // Bottom bar — manual-entry fallback (SSID / password)
    lv_obj_t* bar = makePanel(_scrPortal, 0, 148, SCR_W, SCR_H - 148, C_PANEL_BG);
    makePanel(_scrPortal, 0, 148, SCR_W, 1, C_GOLD);
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
        lv_label_set_text(_lblWaitSub, "MINUTE WAIT");  // reads right for 1 and many
    }

    _lastWaitTime = ride.waitTime;
    _lastIsOpen   = ride.isOpen;
}

// ---------------------------------------------------------------------------
// _setRideName
// ---------------------------------------------------------------------------
void DisplayController::_setRideName(const String& name) {
    lv_label_set_text(_lblRideName, name.c_str());
    _lastRideName = name;
}

// ---------------------------------------------------------------------------
// Public API — main screen
// ---------------------------------------------------------------------------

void DisplayController::drawBackground() {
    _loadMain();
}

void DisplayController::drawParkName(const String& parkName, bool force) {
    if (!force && parkName == _lastParkName) {
        lv_label_set_text(_lblTime, clockText().c_str());
        return;
    }
    _lastParkName = parkName;
    lv_label_set_text(_lblPark, parkName.c_str());
    lv_label_set_text(_lblTime, clockText().c_str());
}

void DisplayController::setRideCount(int count) {
    _rideCount = count;
}

void DisplayController::drawProgressBar(int currentIdx, int totalCount) {
    if (totalCount <= 0) {
        lv_bar_set_value(_barProgress, 0, LV_ANIM_OFF);
        lv_label_set_text(_lblRideIdx, "");
        return;
    }
    int pct = (currentIdx + 1) * 100 / totalCount;
    lv_bar_set_value(_barProgress, pct, LV_ANIM_ON);
    char buf[10];
    snprintf(buf, sizeof(buf), "%d/%d", currentIdx + 1, totalCount);
    lv_label_set_text(_lblRideIdx, buf);
}

void DisplayController::displayRide(const RideInfo& ride, int rideIdx) {
    _loadMain();
    _setRideName(ride.name);
    if (_rideCount > 0) drawProgressBar(rideIdx, _rideCount);
    _applyWaitWidgets(ride);
    _lastRideIdx = rideIdx;
}

void DisplayController::updateRideIfChanged(const RideInfo& ride, int rideIdx) {
    const bool nameChanged = (ride.name != _lastRideName);
    const bool idxChanged  = (rideIdx  != _lastRideIdx);
    const bool waitChanged = (ride.waitTime != _lastWaitTime ||
                              ride.isOpen   != _lastIsOpen);

    if (!nameChanged && !idxChanged && !waitChanged) return;

    if (nameChanged) _setRideName(ride.name);
    if (idxChanged && _rideCount > 0) {
        drawProgressBar(rideIdx, _rideCount);
        _lastRideIdx = rideIdx;
    }
    if (waitChanged) _applyWaitWidgets(ride);
}

void DisplayController::redrawWaitTime(const RideInfo& ride) {
    _applyWaitWidgets(ride);
}

void DisplayController::setDataFreshness(int ageMinutes) {
    if (_objGoldSep == nullptr) return;
    lv_color_t c = (ageMinutes < 5)  ? C_GOLD :
                   (ageMinutes < 15) ? C_GOLD_DIM : C_GOLD_OLD;
    lv_obj_set_style_bg_color(_objGoldSep, c, LV_PART_MAIN);
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

void DisplayController::showClosedPark(const String& parkName) {
    _loadMain();
    drawParkName(parkName, true);
    _setRideName("All rides closed");
    lv_bar_set_value(_barProgress, 0, LV_ANIM_OFF);
    lv_label_set_text(_lblRideIdx, "");

    // Teal theme for closed park
    lv_obj_set_style_bg_color(_objRideAccent, T_TEAL.accent, LV_PART_MAIN);
    lv_obj_set_style_bg_color(_barProgress, T_TEAL.accent, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(_objWaitPanel, T_TEAL.bgBot, LV_PART_MAIN);
    lv_obj_set_style_bg_color(_objWaitBorder, T_TEAL.accent, LV_PART_MAIN);
    lv_obj_set_style_text_color(_lblWaitNum, T_TEAL.accent, LV_PART_MAIN);
    lv_obj_set_style_text_font(_lblWaitNum, &lv_font_montserrat_48, LV_PART_MAIN);
    lv_label_set_text(_lblWaitNum, "CLOSED");
    lv_label_set_text(_lblWaitSub, "PARK IS CLOSED TODAY");
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
