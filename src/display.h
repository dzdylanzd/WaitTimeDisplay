#ifndef DISPLAY_H
#define DISPLAY_H

#include <Arduino.h>
#include <lvgl.h>
#include "queueapi.h"

// What showNoData() should explain to the user. Each reason gets its own
// title, colour, and recovery hint — see DisplayController::showNoData().
// (NO_PARKS/NO_RIDES/FETCH_FAILED names avoid Windows' NO_DATA macro,
//  which the simulator build pulls in via winsock.)
enum class NoDataReason {
  NO_PARKS,      // config is empty — point the user at the web UI
  NO_RIDES,      // park returned rides but the filter removed them all
  FETCH_FAILED,  // API fetch failed while WiFi is up
  WIFI_LOST,     // WiFi dropped; AppStateManager is reconnecting
  WIFI_TROUBLE   // repeated connect failures; still retrying + recovery hint
};

/**
 * DisplayController — LVGL renderer for QueueWatch.
 *
 * Three persistent LVGL screens, built once at begin():
 *   _scrMain   — ride display (header, progress, ride name, wait-time panel)
 *   _scrStatus — info/error/splash (connecting, startup, errors)
 *   _scrPortal — WiFi captive-portal setup with scannable QR code
 *
 * Call lv_timer_handler() every loop() iteration.
 */
class DisplayController {
public:
  DisplayController();

  void begin();
  void drawBackground();
  void drawParkName(const String& parkName, bool force);
  void setHeaderInfo(const String& info);  // small label above the clock
                                           // (today's park hours; "" hides it)
  void setRideCount(int count);
  void drawProgressBar(int currentIdx, int totalCount, bool favorite = false);
  void displayRide(const RideInfo& ride, int rideIdx = 0);
  void updateRideIfChanged(const RideInfo& ride, int rideIdx);
  void redrawWaitTime(const RideInfo& ride);

  void showNoData(NoDataReason reason = NoDataReason::FETCH_FAILED);
  void showFactoryResetWarning();   // BOOT held 10 s — "keep holding to erase"
  void showFactoryResetting();      // BOOT held 20 s — painted just before restart
  // OTA update in progress (web-UI triggered). Both force an immediate
  // redraw, like showFactoryResetting(), since the caller is blocked inside
  // a long-running download/flash and lv_timer_handler() won't run again
  // until it returns.
  void showOtaDownloading(uint8_t progressPct);
  void showOtaInstalling();         // painted just before Update.end() + restart
  // subText overrides the wait panel's sub-label when non-empty (e.g.
  // "OPENS 09:00" from the park schedule); default: "PARK IS CLOSED TODAY".
  void showClosedPark(const String& parkName, const String& subText = "");
  void showCaptivePortalInfo(const char* apName, const char* apPass);
  void showStartupInfo(const String& ipAddress);
  void showConnectingScreen(int dotCount);

  // Gold separator dims when data is stale: 0–4 min = gold, 5–14 = amber, 15+ = red
  void setDataFreshness(int ageMinutes);

  // Switch the UI chrome palette (0 = Magic Night default). Restyles all
  // three screens in place — no reboot needed. Wait-time colours (green/
  // amber/orange/red/teal) are semantic and stay the same in every palette.
  void applyPalette(uint8_t paletteId);

  // Define the user "Custom" palette (the last colorPalette index) from three
  // picked 0xRRGGBB colours — header background, accent, ride-panel background.
  // The remaining palette colours (text shades, dims, status backgrounds) are
  // derived for legibility. Call before applyPalette() selects the Custom slot.
  void setCustomPalette(uint32_t hdr, uint32_t accent, uint32_t panel);

  // User-configured wait-time thresholds (minutes) + level colours
  // (0xRRGGBB, indexed by (int)WaitLevel). Panel backgrounds are derived
  // from each colour; the caller triggers the repaint.
  void setWaitConfig(uint8_t th1, uint8_t th2, uint8_t th3,
                     const uint32_t colors[5]);

private:
  // ---- State tracking ----
  int    _rideCount    = 0;
  String _lastParkName;
  String _headerInfo;
  String _lastRideName;
  String _lastLand;
  int    _lastWaitTime = -999;
  RideStatus _lastStatus  = RideStatus::Closed;
  EntityKind _lastKind    = EntityKind::Attraction;
  int16_t    _lastShowMin = -1;
  int    _lastRideIdx  = -1;
  int8_t _lastTrend    = 0;
  bool   _lastFavorite = false;
  int    _lastAgeMin   = 0;    // re-applied by applyPalette()

  // ---- Main ride screen ----
  lv_obj_t* _scrMain       = nullptr;
  lv_obj_t* _lblPark       = nullptr;
  lv_obj_t* _lblHeaderInfo = nullptr;  // small info line above the clock (park hours)
  lv_obj_t* _lblTime       = nullptr;
  lv_obj_t* _barProgress   = nullptr;
  lv_obj_t* _lblRideIdx    = nullptr;
  lv_obj_t* _lblRideName   = nullptr;
  lv_obj_t* _lblLand       = nullptr;  // themed-land name (2nd ride-panel row)
  lv_obj_t* _lblTrend      = nullptr;  // rising/falling arrow + delta minutes
  lv_obj_t* _objWaitPanel  = nullptr;  // full-width coloured background
  lv_obj_t* _objWaitBorder = nullptr;  // 2 px accent line at top of wait panel
  lv_obj_t* _lblWaitNum    = nullptr;  // big number / "CLOSED"
  lv_obj_t* _lblWaitSub    = nullptr;  // "minutes" / status sub-text
  lv_obj_t* _objGoldSep    = nullptr;  // 1 px separator below header (dims when stale)
  lv_obj_t* _objRideAccent = nullptr;  // 4 px left stripe on ride panel (theme color)
  lv_obj_t* _pnlHdr        = nullptr;  // header panel (palette restyle)
  lv_obj_t* _pnlRide       = nullptr;  // ride panel (palette restyle)

  // ---- Status screen ----
  lv_obj_t* _scrStatus   = nullptr;
  lv_obj_t* _lblStTitle  = nullptr;
  lv_obj_t* _lblStSub    = nullptr;
  lv_obj_t* _lblStBody   = nullptr;
  lv_obj_t* _objStBottom = nullptr;  // bottom accent panel
  lv_obj_t* _lblStExtra  = nullptr;  // IP / URL in bottom panel
  lv_obj_t* _sepStatus   = nullptr;  // separator under title (palette restyle)
  lv_obj_t* _lineStBottom = nullptr; // 1 px line above bottom panel

  // ---- Captive-portal screen (Wi-Fi QR code) ----
  lv_obj_t* _scrPortal    = nullptr;
  lv_obj_t* _qrPortal     = nullptr;  // scannable Wi-Fi join code
  lv_obj_t* _lblPortalNet = nullptr;  // SSID / password fallback text
  lv_obj_t* _lblPortalTitle = nullptr;
  lv_obj_t* _lblPortalBody  = nullptr;
  lv_obj_t* _sepPortal      = nullptr;
  lv_obj_t* _pnlPortalBar   = nullptr;
  lv_obj_t* _linePortalBar  = nullptr;

  void _buildMainScreen();
  void _buildStatusScreen();
  void _buildPortalScreen();
  void _loadMain();
  void _loadStatus();
  void _loadPortal();
  void _applyWaitWidgets(const RideInfo& ride);
  void _setRideName(const String& name);
  void _setLand(const String& land);
};

#endif // DISPLAY_H
