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

enum class SystemState {
  BOOT,
  WIFI_CONFIG_PORTAL,
  WIFI_CONNECTING,
  STARTUP_INFO,
  WAIT_TIME_CYCLE,
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

  bool allRidesClosed() const;
  void applyRideFilter();
  void annotateRides();
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
  int      _currentParkId = 0;
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
  bool         _startupScreenShown = false;
  bool         _showingClosedPark  = false;
  unsigned long _closedParkStart   = 0;

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
};

#endif // APPSTATE_H
