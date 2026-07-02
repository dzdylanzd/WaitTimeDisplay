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
                  ConfigWebServer& webServer);

  void begin();
  void update();

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

  bool allRidesClosed() const;
  void applyRideFilter();
  void loadParkData();
  void refreshRideData();
  void advanceToNextPark();
  void reloadRuntimeConfig();
  void restartCycle();
  void transitionTo(SystemState newState);
  void syncParkTimezone(bool force = false);
  void resetCycleTimers();
  void rememberRideNames();

  WiFiManager&       _wifi;
  QueueApi&          _api;
  ConfigManager&     _cfg;
  DisplayController& _display;
  ConfigWebServer&   _webServer;

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

  bool         _startupScreenShown = false;
  bool         _showingClosedPark  = false;
  unsigned long _closedParkStart   = 0;

  int           _wifiFailCount     = 0;
  unsigned long _lastWiFiTry       = 0;
  unsigned long _wifiConnectStart  = 0;
  int           _connectingDotCount = 0;
  unsigned long _lastDotUpdate     = 0;
  unsigned long _wifiBeginTime     = 0;
};

#endif // APPSTATE_H
