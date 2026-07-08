#ifndef APPSTATE_H
#define APPSTATE_H

#include <Arduino.h>
#include <vector>
#include "config.h"
#include "wifimgr.h"
#include "queueapi.h"
#include "display.h"
#include "configmanager.h"
#include "cfgserver.h"
#include "trendstore.h"
#include "statusled.h"
#include "button.h"
#include "otaupdater.h"

enum class SystemState {
  BOOT,
  WIFI_CONFIG_PORTAL,
  WIFI_CONNECTING,
  STARTUP_INFO,
  WAIT_TIME_CYCLE,
  // Reached only from WAIT_TIME_CYCLE via a web-UI-triggered "Install"
  // click (see ConfigWebServer::consumeOtaStartRequest()) — checking for an
  // update happens synchronously inside the web request handler itself
  // (ConfigWebServer::handleApiOtaCheck()), so by the time this state is
  // entered the asset URL to download is already known.
  OTA_DOWNLOADING,
  OTA_FLASHING,
  RECONNECTING,
  NO_PARKS_CONFIGURED
};

class AppStateManager {
public:
  AppStateManager(WiFiManager& wifi, QueueApi& api,
                  ConfigManager& cfg, DisplayController& display,
                  ConfigWebServer& webServer, StatusLed& led);

  void begin();
  void update();

  // BOOT button: Short = next ride, Long = next park (WAIT_TIME_CYCLE only)
  void onButtonEvent(ButtonEvent ev);

  SystemState getState() const { return _state; }

private:
  void enterWifiConfigPortal();
  void tickWifiConfigPortal();
  void enterWifiConnecting();
  void tickWifiConnecting(unsigned long now);
  void tickStartupInfo(unsigned long now);
  void tickWaitTimeCycle(unsigned long now);
  void enterReconnecting();
  void tickReconnecting(unsigned long now);
  void enterNoParksConfigured();
  void enterWifiTroubleScreen();
  void enterOtaDownloading();
  void enterOtaFlashing();
  static void otaProgressThunk(size_t written, size_t total);

  bool allRidesClosed() const;
  void showClosedParkScreen(int nowMin);
  void applyRideFilter();
  void annotateRides();
  void stampShownTrend(RideInfo& r);
  void applyRideDisplayOptions();
  bool fetchAndProcessRideData();
  void loadParkData();
  void refreshRideData();
  void advanceRide();
  void advanceToNextPark();
  void reloadRuntimeConfig();
  void restartCycle();
  void transitionTo(SystemState newState);
  void syncParkTimezone(bool force = false);
  void resetCycleTimers();
  void rememberRideNames();
  void repaintAfterResetWarning();
  void applyBrightness(bool force = false);
  void applyScreenFlip();
  void applyColorPalette();
  void applyWaitConfig();
  uint8_t effectiveBrightness() const;
  void updateLed();

  // The OTA progress callback is a plain function pointer (OtaProgressFn),
  // not a capturing lambda/std::function, so it can't reach `this` — this
  // static pointer is the trampoline's only way back to the instance. Safe
  // because exactly one AppStateManager exists (matches the rest of this
  // codebase's singleton-per-concern pattern: one DisplayController, one
  // ConfigWebServer, etc.), and performUpdate() is only ever in flight from
  // within this same instance's tick, never concurrently.
  static AppStateManager* _instance;

  WiFiManager&       _wifi;
  QueueApi&          _api;
  ConfigManager&     _cfg;
  DisplayController& _display;
  ConfigWebServer&   _webServer;
  StatusLed&         _led;

  SystemState   _state = SystemState::BOOT;
  unsigned long _stateEnterTime = 0;

  RideInfo _rides[MAX_RIDES];
  int      _rideCount = 0;
  int      _currentRideIndex = 0;
  int      _currentParkIndex = 0;
  String   _currentParkId;             // dashed themeparks.wiki UUID
  String   _currentParkName;
  String   _currentTimezone = "UTC";

  String   _lastRideNames[MAX_RIDES];

  unsigned long _lastApiFetch   = 0;
  unsigned long _lastRotate     = 0;
  unsigned long _lastTimeUpdate = 0;

  unsigned long _cfgApiRefreshInterval    = DEFAULT_API_REFRESH_INTERVAL;
  unsigned long _cfgRotateInterval        = DEFAULT_ROTATE_INTERVAL;
  unsigned long _cfgClosedParkDisplayTime = DEFAULT_CLOSED_PARK_DISPLAY_TIME;
  unsigned long _cfgTimeUpdateInterval    = DEFAULT_TIME_UPDATE_INTERVAL;

  int          _parkLoadFailures  = 0;
  // Self-healing for the "every fetch fails while WiFi still claims to be
  // connected" failure modes (zombie association, TLS heap exhaustion):
  // consecutive failed fetches and when the current failure streak started.
  int           _fetchFailStreak = 0;
  unsigned long _fetchFailSince  = 0;
  bool          _wifiCycleTried  = false;  // one forced reconnect per streak
  int           _fetchFailStreakAtCycle = 0;  // streak value when it was tried
  bool         _startupScreenShown = false;
  bool         _showingClosedPark  = false;
  unsigned long _closedParkStart   = 0;
  // Today's operating window for the current park (refetched per park load;
  // date-stamped, so QueueApi refreshes it when the day rolls over) and
  // whether the closed screen is up because the schedule says so — that
  // suppresses /live polling until opening time (single-park case).
  ParkHours    _parkHours;
  bool         _closedBySchedule   = false;

  TrendStore    _trends;
  uint8_t       _lastAppliedBrightness = 0;
  bool          _brightnessApplied     = false;  // false until the first apply
  unsigned long _lastBrightnessCheck   = 0;
  bool          _resetWarningActive    = false; // BOOT held ≥10 s, screen frozen
  bool          _lastAppliedFlip       = false; // LCD_Init leaves it unflipped
  uint8_t       _lastAppliedPalette    = 0;     // screens are built in palette 0

  int           _wifiFailCount     = 0;
  bool          _wifiTrouble       = false;  // trouble screen shown, retrying
  unsigned long _lastWiFiTry       = 0;
  unsigned long _wifiConnectStart  = 0;
  int           _connectingDotCount = 0;
  unsigned long _lastDotUpdate     = 0;
  unsigned long _wifiBeginTime     = 0;

  OtaUpdater _ota;
  String     _otaAssetUrl;               // set by tickWaitTimeCycle() when the
                                          // web UI's install request is consumed
  // Post-OTA rollback confirmation: an in-RAM (not persisted) counter of
  // consecutive successful wait-time fetches since boot. Only relevant when
  // OtaUpdater::isPendingConfirmation() is true (see appstate.cpp).
  int  _otaConfirmFetchCount = 0;
  bool _otaConfirmed         = false;    // true once markBootSuccessful() has run
};

#endif // APPSTATE_H
