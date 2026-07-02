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
  WIFI_LOST      // WiFi dropped; AppStateManager is reconnecting
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
  void setRideCount(int count);
  void drawProgressBar(int currentIdx, int totalCount);
  void displayRide(const RideInfo& ride, int rideIdx = 0);
  void updateRideIfChanged(const RideInfo& ride, int rideIdx);
  void redrawWaitTime(const RideInfo& ride);

  void showNoData(NoDataReason reason = NoDataReason::FETCH_FAILED);
  void showClosedPark(const String& parkName);
  void showCaptivePortalInfo(const char* apName, const char* apPass);
  void showStartupInfo(const String& ipAddress);
  void showConnectingScreen(int dotCount);

  // Gold separator dims when data is stale: 0–4 min = gold, 5–14 = amber, 15+ = red
  void setDataFreshness(int ageMinutes);

private:
  // ---- State tracking ----
  int    _rideCount    = 0;
  String _lastParkName;
  String _lastRideName;
  int    _lastWaitTime = -999;
  bool   _lastIsOpen   = false;
  int    _lastRideIdx  = -1;

  // ---- Main ride screen ----
  lv_obj_t* _scrMain       = nullptr;
  lv_obj_t* _lblPark       = nullptr;
  lv_obj_t* _lblTime       = nullptr;
  lv_obj_t* _barProgress   = nullptr;
  lv_obj_t* _lblRideIdx    = nullptr;
  lv_obj_t* _lblRideName   = nullptr;
  lv_obj_t* _objWaitPanel  = nullptr;  // full-width coloured background
  lv_obj_t* _objWaitBorder = nullptr;  // 2 px accent line at top of wait panel
  lv_obj_t* _lblWaitNum    = nullptr;  // big number / "CLOSED"
  lv_obj_t* _lblWaitSub    = nullptr;  // "minutes" / status sub-text
  lv_obj_t* _objGoldSep    = nullptr;  // 1 px separator below header (dims when stale)
  lv_obj_t* _objRideAccent = nullptr;  // 4 px left stripe on ride panel (theme color)

  // ---- Status screen ----
  lv_obj_t* _scrStatus   = nullptr;
  lv_obj_t* _lblStTitle  = nullptr;
  lv_obj_t* _lblStSub    = nullptr;
  lv_obj_t* _lblStBody   = nullptr;
  lv_obj_t* _objStBottom = nullptr;  // bottom accent panel
  lv_obj_t* _lblStExtra  = nullptr;  // IP / URL in bottom panel

  // ---- Captive-portal screen (Wi-Fi QR code) ----
  lv_obj_t* _scrPortal    = nullptr;
  lv_obj_t* _qrPortal     = nullptr;  // scannable Wi-Fi join code
  lv_obj_t* _lblPortalNet = nullptr;  // SSID / password fallback text

  void _buildMainScreen();
  void _buildStatusScreen();
  void _buildPortalScreen();
  void _loadMain();
  void _loadStatus();
  void _loadPortal();
  void _applyWaitWidgets(const RideInfo& ride);
  void _setRideName(const String& name);
};

#endif // DISPLAY_H
