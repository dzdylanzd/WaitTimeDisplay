#include <WiFi.h>
#include "appstate.h"
#include "tzhelper.h"

AppStateManager::AppStateManager(WiFiManager& wifi, QueueApi& api,
                                  ConfigManager& cfg, DisplayController& display,
                                  ConfigWebServer& webServer)
  : _wifi(wifi), _api(api), _cfg(cfg),
    _display(display), _webServer(webServer) {}

void AppStateManager::begin() {
  reloadRuntimeConfig();
  if (!_wifi.isConfigured()) transitionTo(SystemState::WIFI_CONFIG_PORTAL);
  else                       transitionTo(SystemState::WIFI_CONNECTING);
}

void AppStateManager::update() {
  unsigned long now = millis();

  if (_webServer.isConfigUpdated()) {
    _webServer.clearConfigFlag();
    restartCycle();
    transitionTo(SystemState::WAIT_TIME_CYCLE);
    return;
  }

  switch (_state) {
    case SystemState::WIFI_CONFIG_PORTAL: tickWifiConfigPortal();        break;
    case SystemState::WIFI_CONNECTING:    tickWifiConnecting(now);       break;
    case SystemState::STARTUP_INFO:       tickStartupInfo(now);          break;
    case SystemState::WAIT_TIME_CYCLE:    tickWaitTimeCycle(now);        break;
    case SystemState::RECONNECTING:       tickReconnecting(now);         break;
    case SystemState::NO_PARKS_CONFIGURED: break;
    default:                               break;
  }
}

// ==================================================================
// WIFI_CONFIG_PORTAL
// ==================================================================

void AppStateManager::enterWifiConfigPortal() {
  _webServer.stop();
  _display.showCaptivePortalInfo("QueueWatch-Config", "config123");
  _wifi.runCaptivePortal();
}

void AppStateManager::tickWifiConfigPortal() {
  transitionTo(SystemState::WIFI_CONNECTING);
}

// ==================================================================
// WIFI_CONNECTING
// ==================================================================

void AppStateManager::enterWifiConnecting() {
  _wifiConnectStart = millis();
  _connectingDotCount = 0;
  _lastDotUpdate = 0;
  _display.showConnectingScreen(0);
}

void AppStateManager::tickWifiConnecting(unsigned long now) {
  if (now - _wifiConnectStart >= WIFI_CONNECT_TIMEOUT_MS) {
    // Keep the stored credentials: the network may simply not be in range
    // (device powered on somewhere else). The portal only overwrites them
    // when the user saves new ones; a power cycle back home reconnects.
    _wifi.resetConnecting();
    transitionTo(SystemState::WIFI_CONFIG_PORTAL);
    return;
  }

  // Animate connecting dots every 500 ms
  if (now - _lastDotUpdate >= 500) {
    _connectingDotCount = (_connectingDotCount + 1) & 3;
    _display.showConnectingScreen(_connectingDotCount);
    _lastDotUpdate = now;
  }

  _wifi.connect();
  if (WiFi.status() == WL_CONNECTED) {
    _webServer.begin();  // revive the config UI if a portal session stopped it
    transitionTo(SystemState::STARTUP_INFO);
  }
}

// ==================================================================
// STARTUP_INFO
// ==================================================================

void AppStateManager::tickStartupInfo(unsigned long now) {
  if (!_startupScreenShown) {
    _display.showStartupInfo(WiFi.localIP().toString());
    _startupScreenShown = true;
  }
  if (now - _stateEnterTime >= STARTUP_SPLASH_DURATION) {
    _startupScreenShown = false;
    const RuntimeConfig& cfg = _cfg.getConfig();
    if (cfg.enabledParkIds.size() > 0) {
      restartCycle();
      transitionTo(SystemState::WAIT_TIME_CYCLE);
    } else {
      transitionTo(SystemState::NO_PARKS_CONFIGURED);
    }
  }
}

// ==================================================================
// WAIT_TIME_CYCLE
// ==================================================================

void AppStateManager::tickWaitTimeCycle(unsigned long now) {
  if (WiFi.status() != WL_CONNECTED) {
    transitionTo(SystemState::RECONNECTING);
    return;
  }

  const RuntimeConfig& cfg = _cfg.getConfig();
  if (cfg.enabledParkIds.size() == 0) {
    _display.showNoData(NoDataReason::NO_PARKS);
    return;
  }

  if (_rideCount <= 0) {
    if (now - _lastApiFetch >= DATA_RETRY_INTERVAL) {
      _lastApiFetch = now;
      loadParkData();
    }
    return;
  }

  if (allRidesClosed()) {
    if (!_showingClosedPark) {
      _display.showClosedPark(_currentParkName);
      _showingClosedPark = true;
      _closedParkStart = now;
    } else if (now - _closedParkStart >= _cfgClosedParkDisplayTime) {
      advanceToNextPark();
    }
    if (now - _lastTimeUpdate >= _cfgTimeUpdateInterval) {
      _display.showClosedPark(_currentParkName);
      _lastTimeUpdate = now;
    }
    return;
  }

  _showingClosedPark = false;

  if (now - _lastTimeUpdate >= _cfgTimeUpdateInterval) {
    int ageMin = (int)((now - _lastApiFetch) / 60000UL);
    _display.setDataFreshness(ageMin);
    _display.drawParkName(_currentParkName, true);
    _display.redrawWaitTime(_rides[_currentRideIndex]);
    _lastTimeUpdate = now;
  }

  if (now - _lastRotate >= _cfgRotateInterval) {
    _currentRideIndex++;
    if (_currentRideIndex >= _rideCount) {
      advanceToNextPark();
    } else {
      _display.drawParkName(_currentParkName, false);
      _display.updateRideIfChanged(_rides[_currentRideIndex], _currentRideIndex);
    }
    _lastRotate = now;
  }

  if (now - _lastApiFetch >= _cfgApiRefreshInterval) {
    refreshRideData();
    _lastApiFetch = now;  // also on failure — rate-limits retries
  }
}

// Re-fetch the current park's rides and repaint only as much as changed:
// nothing, just the wait panel (values moved), or the whole screen
// (rides added / removed / renamed / reordered).
void AppStateManager::refreshRideData() {
  int  prevCount = _rideCount;
  int  prevId[MAX_RIDES];
  int  prevWait[MAX_RIDES];
  bool prevOpen[MAX_RIDES];
  for (int i = 0; i < prevCount; i++) {
    prevId[i]   = _rides[i].id;
    prevWait[i] = _rides[i].waitTime;
    prevOpen[i] = _rides[i].isOpen;
  }

  if (!_api.fetchRideData(_currentParkId, _rides, _rideCount, MAX_RIDES)) return;

  applyRideFilter();
  _display.setRideCount(_rideCount);
  _display.setDataFreshness(0);
  if (_rideCount > 0 && _currentRideIndex >= _rideCount) _currentRideIndex = 0;

  bool structureChanged = (_rideCount != prevCount);
  bool waitsChanged     = false;
  for (int i = 0; !structureChanged && i < _rideCount; i++) {
    if (_rides[i].id != prevId[i] || _rides[i].name != _lastRideNames[i])
      structureChanged = true;
    else if (_rides[i].waitTime != prevWait[i] || _rides[i].isOpen != prevOpen[i])
      waitsChanged = true;
  }

  rememberRideNames();

  if (_rideCount == 0) return;  // allRidesClosed()/loadParkData paths handle empty
  if (structureChanged) {
    _display.drawBackground();
    _display.drawParkName(_currentParkName, true);
    _display.displayRide(_rides[_currentRideIndex], _currentRideIndex);
  } else if (waitsChanged) {
    _display.redrawWaitTime(_rides[_currentRideIndex]);
  }
}

// ==================================================================
// RECONNECTING
// ==================================================================

void AppStateManager::enterReconnecting() {
  _display.showNoData(NoDataReason::WIFI_LOST);
}

void AppStateManager::tickReconnecting(unsigned long now) {
  if (now - _lastWiFiTry < WIFI_RECONNECT_INTERVAL) { return; }
  _lastWiFiTry = now;

  // connect() returns false the whole time an attempt is in flight, so only
  // stamp the attempt start once — otherwise the 15 s timeout below can
  // never fire (the timestamp would be refreshed every retry tick).
  bool inProgress = !_wifi.connect();
  if (inProgress && _wifiBeginTime == 0) _wifiBeginTime = now;

  if (WiFi.status() == WL_CONNECTED) {
    _wifiFailCount = 0;
    _wifiBeginTime = 0;
    _wifi.resetConnecting();
    syncParkTimezone(true);  // force: re-sync NTP after the outage
    resetCycleTimers();
    _webServer.begin();
    transitionTo(SystemState::WAIT_TIME_CYCLE);
    return;
  }

  bool attemptTimedOut   = (_wifiBeginTime > 0 && (now - _wifiBeginTime >= 15000UL));
  bool terminalFailure   = (WiFi.status() == WL_CONNECT_FAILED ||
                            WiFi.status() == WL_NO_SSID_AVAIL);
  bool disconnectedAfter = (WiFi.status() == WL_DISCONNECTED && attemptTimedOut);

  if (terminalFailure || disconnectedAfter) {
    _wifiFailCount++;
    _wifi.resetConnecting();
    _wifiBeginTime = 0;
    if (_wifiFailCount >= 3) {
      _wifiFailCount = 0;
      // Credentials are intentionally kept — see tickWifiConnecting()
      transitionTo(SystemState::WIFI_CONFIG_PORTAL);
    }
  }
}

// ==================================================================
// transitionTo
// ==================================================================

void AppStateManager::transitionTo(SystemState newState) {
  _state = newState;
  _stateEnterTime = millis();
  if (newState == SystemState::STARTUP_INFO) _startupScreenShown = false;
  switch (newState) {
    case SystemState::WIFI_CONFIG_PORTAL: enterWifiConfigPortal(); break;
    case SystemState::WIFI_CONNECTING:    enterWifiConnecting();    break;
    case SystemState::RECONNECTING:       enterReconnecting();      break;
    default: break;
  }
}

// ==================================================================
// Helpers
// ==================================================================

bool AppStateManager::allRidesClosed() const {
  if (_rideCount <= 0) return true;
  for (int i = 0; i < _rideCount; i++) if (_rides[i].isOpen) return false;
  return true;
}

void AppStateManager::applyRideFilter() {
  int writeIdx = 0;
  for (int i = 0; i < _rideCount; i++) {
    if (_cfg.isRideEnabled(_currentParkId, _rides[i].id))
      _rides[writeIdx++] = _rides[i];
  }
  _rideCount = writeIdx;
}

// Full load of the current park: sync its timezone, fetch rides, and paint
// the appropriate screen from scratch.
void AppStateManager::loadParkData() {
  syncParkTimezone();

  if (_api.fetchRideData(_currentParkId, _rides, _rideCount, MAX_RIDES)) {
    applyRideFilter();
    _display.setRideCount(_rideCount);
    _display.setDataFreshness(0);
    _showingClosedPark = false;
    if (_rideCount > 0) {
      _display.drawBackground();
      _display.drawParkName(_currentParkName, true);
      _currentRideIndex = 0;
      _display.displayRide(_rides[_currentRideIndex], _currentRideIndex);
    } else {
      _display.showNoData(NoDataReason::NO_RIDES);
    }
  } else {
    _display.showNoData(NoDataReason::FETCH_FAILED);
    _rideCount = 0;
  }
  rememberRideNames();
}

void AppStateManager::advanceToNextPark() {
  const RuntimeConfig& cfg = _cfg.getConfig();
  if (cfg.enabledParkIds.size() == 0) {
    _display.showNoData(NoDataReason::NO_PARKS);
    return;
  }
  _currentParkIndex = (_currentParkIndex + 1) % (int)cfg.enabledParkIds.size();
  _currentParkId    = cfg.enabledParkIds[_currentParkIndex];
  _currentParkName  = cfg.enabledParkNames[_currentParkIndex];
  loadParkData();
  resetCycleTimers();
}

void AppStateManager::reloadRuntimeConfig() {
  const RuntimeConfig& cfg = _cfg.getConfig();
  _cfgApiRefreshInterval    = cfg.apiRefreshInterval;
  _cfgRotateInterval        = cfg.rotateInterval;
  _cfgClosedParkDisplayTime = cfg.closedParkDisplayTime;
  _cfgTimeUpdateInterval    = cfg.timeUpdateInterval;
}

// Restart the park cycle from the first configured park. Called at startup
// and whenever the web UI saves a new configuration.
void AppStateManager::restartCycle() {
  reloadRuntimeConfig();
  _wifiFailCount = 0;
  const RuntimeConfig& cfg = _cfg.getConfig();
  if (cfg.enabledParkIds.size() == 0) {
    _display.showNoData(NoDataReason::NO_PARKS);
    return;
  }

  // Pre-warm the timezone cache so later park switches don't need a lookup
  for (size_t i = 0; i < cfg.enabledParkIds.size(); i++)
    _api.getParkTimezone(cfg.enabledParkIds[i]);

  _currentParkIndex = 0;
  _currentParkId    = cfg.enabledParkIds[0];
  _currentParkName  = cfg.enabledParkNames[0];

  loadParkData();  // syncs the park timezone itself
  resetCycleTimers();
}

// ------------------------------------------------------------------
// Small shared helpers
// ------------------------------------------------------------------

// Look up the current park's IANA timezone and apply it (NTP re-sync).
// force=true re-applies even when unchanged, e.g. after a WiFi outage.
void AppStateManager::syncParkTimezone(bool force) {
  String tz = _api.getParkTimezone(_currentParkId);
  bool changed = (tz != _currentTimezone);
  if (changed) _currentTimezone = tz;
  if (changed || force) applyTimeZone(_currentTimezone);
}

// Restart the fetch/rotate/clock timers so a fresh park or reconnect gets
// its full display interval.
void AppStateManager::resetCycleTimers() {
  unsigned long now = millis();
  _lastApiFetch   = now;
  _lastRotate     = now;
  _lastTimeUpdate = now;
}

// Snapshot ride names for the next refresh's change detection.
void AppStateManager::rememberRideNames() {
  for (int i = 0; i < MAX_RIDES; i++)
    _lastRideNames[i] = (i < _rideCount) ? _rides[i].name : String();
}
