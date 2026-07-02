#include <WiFi.h>
#include "appstate.h"
#include "tzhelper.h"
#include "quiethours.h"
#include "ridefilter.h"
#include "waitlevel.h"
#include "lcd_st7789.h"

// How often the quiet-hours window / brightness setting is re-evaluated.
static constexpr unsigned long BRIGHTNESS_CHECK_INTERVAL = 10000UL;

AppStateManager::AppStateManager(WiFiManager& wifi, QueueApi& api,
                                  ConfigManager& cfg, DisplayController& display,
                                  ConfigWebServer& webServer, StatusLed& led)
  : _wifi(wifi), _api(api), _cfg(cfg),
    _display(display), _webServer(webServer), _led(led) {}

void AppStateManager::begin() {
  reloadRuntimeConfig();
  applyBrightness(true);   // LCD_Init leaves the backlight at 100%
  applyScreenFlip();       // ...and the panel unflipped
  if (!_wifi.isConfigured()) transitionTo(SystemState::WIFI_CONFIG_PORTAL);
  else                       transitionTo(SystemState::WIFI_CONNECTING);
}

void AppStateManager::update() {
  unsigned long now = millis();

  // Quiet hours must also dim status/error screens, so this runs in every
  // state, on a coarse timer.
  if (now - _lastBrightnessCheck >= BRIGHTNESS_CHECK_INTERVAL) {
    _lastBrightnessCheck = now;
    if (!_resetWarningActive) applyBrightness();
  }

  // The factory-reset warning freezes everything: no rotation, no refresh,
  // no screen changes until the button is released (cancel) or held to 20 s.
  if (_resetWarningActive) return;

  if (_webServer.isConfigUpdated()) {
    _webServer.clearConfigFlag();
    applyScreenFlip();       // before the repaint restartCycle triggers
    restartCycle();
    applyBrightness(true);   // pick up a changed brightness immediately
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
      // A park that keeps failing (fetch error or zero usable rides) must not
      // stall the whole cycle: after two failed retries move on to the next
      // configured park instead of hammering the same one forever.
      if (_parkLoadFailures >= 2 && cfg.enabledParkIds.size() > 1) {
        advanceToNextPark();
      } else {
        loadParkData();
      }
    }
    return;
  }

  if (allRidesClosed()) {
    if (!_showingClosedPark) {
      _display.showClosedPark(_currentParkName);
      _showingClosedPark = true;
      _closedParkStart = now;
      updateLed();
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
    advanceRide();
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
  annotateRides();
  applyRideDisplayOptions();
  _display.setRideCount(_rideCount);
  _display.setDataFreshness(0);
  if (_rideCount > 0 && _currentRideIndex >= _rideCount) _currentRideIndex = 0;

  // NOTE: wait-desc sorting can reorder rides between refreshes; the name
  // diff below detects that as a structure change and repaints fully.
  bool structureChanged = (_rideCount != prevCount);
  bool waitsChanged     = false;
  for (int i = 0; !structureChanged && i < _rideCount; i++) {
    if (_rides[i].id != prevId[i] || _rides[i].name != _lastRideNames[i])
      structureChanged = true;
    else if (_rides[i].waitTime != prevWait[i] || _rides[i].isOpen != prevOpen[i] ||
             _rides[i].trend != 0)
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
  updateLed();
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
  if (newState != SystemState::WAIT_TIME_CYCLE) _led.off();
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

// Stamp each freshly fetched ride with its trend vs. the previous refresh
// and its favorite flag. Runs after applyRideFilter(), before sorting —
// TrendStore keys by ride id, so ordering doesn't matter.
void AppStateManager::annotateRides() {
  for (int i = 0; i < _rideCount; i++) {
    RideInfo& r = _rides[i];
    int delta = _trends.updateAndGetDelta(r.id, r.isOpen ? r.waitTime : -1);
    r.trend      = (delta > 0) ? 1 : (delta < 0) ? -1 : 0;
    r.trendDelta = (int16_t)delta;
    r.favorite   = _cfg.isRideFavorite(_currentParkId, r.id);
  }
}

// Full load of the current park: sync its timezone, fetch rides, and paint
// the appropriate screen from scratch.
void AppStateManager::loadParkData() {
  syncParkTimezone();

  if (_api.fetchRideData(_currentParkId, _rides, _rideCount, MAX_RIDES)) {
    applyRideFilter();
    annotateRides();
    applyRideDisplayOptions();
    _display.setRideCount(_rideCount);
    _display.setDataFreshness(0);
    _showingClosedPark = false;
    if (_rideCount > 0) {
      _parkLoadFailures = 0;
      _display.drawBackground();
      _display.drawParkName(_currentParkName, true);
      _currentRideIndex = 0;
      _display.displayRide(_rides[_currentRideIndex], _currentRideIndex);
      updateLed();
    } else {
      _parkLoadFailures++;
      _display.showNoData(NoDataReason::NO_RIDES);
    }
  } else {
    _parkLoadFailures++;
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
  _parkLoadFailures = 0;
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
  _parkLoadFailures = 0;
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

// Filter + sort the ride list per the user's global display options
// (skip closed, min wait, favorites first, sort by wait).
void AppStateManager::applyRideDisplayOptions() {
  const RuntimeConfig& cfg = _cfg.getConfig();
  RideDisplayOptions opt;
  opt.sortMode       = cfg.sortMode;
  opt.favoritesFirst = cfg.favoritesFirst;
  opt.skipClosed     = cfg.skipClosedRides;
  opt.minWaitMinutes = cfg.minWaitMinutes;
  applyDisplayOptions(_rides, _rideCount, opt);
}

// Move to the next ride (wrapping into the next park at the end of the
// list). Shared by the rotate timer and the BOOT button's short press.
void AppStateManager::advanceRide() {
  _currentRideIndex++;
  if (_currentRideIndex >= _rideCount) {
    advanceToNextPark();
  } else {
    _display.drawParkName(_currentParkName, false);
    _display.updateRideIfChanged(_rides[_currentRideIndex], _currentRideIndex);
    updateLed();
  }
  _lastRotate = millis();
}

void AppStateManager::onButtonEvent(ButtonEvent ev) {
  if (ev == ButtonEvent::None) return;

  // Factory-reset hold sequence — works in any state that reaches loop()
  // (the blocking captive portal never polls the button).
  switch (ev) {
    case ButtonEvent::HoldWarning: {
      _resetWarningActive = true;
      // Make sure the warning is actually visible: quiet hours may have the
      // backlight at (or near) zero right now.
      uint8_t brt = _cfg.getConfig().brightness;
      if (brt < 30) brt = 30;
      LCD_SetBacklight(brt);
      _lastAppliedBrightness = brt;
      if (_cfg.getConfig().ledEnabled) _led.showLevel(WaitLevel::Red, brt);
      _display.showFactoryResetWarning();
      return;
    }
    case ButtonEvent::HoldCancel:
      _resetWarningActive = false;
      applyBrightness(true);
      repaintAfterResetWarning();
      resetCycleTimers();   // don't let a stale rotate timer fire instantly
      return;
    case ButtonEvent::HoldReset:
      _display.showFactoryResetting();
      delay(600);   // long enough to read before the panel goes dark
      _cfg.factoryReset();
      ESP.restart();
      return;
    default:
      break;
  }

  if (_state != SystemState::WAIT_TIME_CYCLE) return;
  if (ev == ButtonEvent::Short) {
    if (_rideCount > 0 && !_showingClosedPark) advanceRide();
  } else if (ev == ButtonEvent::Long) {
    advanceToNextPark();
  }
}

// Put back whatever the factory-reset warning replaced.
void AppStateManager::repaintAfterResetWarning() {
  switch (_state) {
    case SystemState::WAIT_TIME_CYCLE:
      if (_rideCount > 0 && allRidesClosed()) {
        _display.showClosedPark(_currentParkName);
      } else if (_rideCount > 0) {
        _display.drawBackground();
        _display.drawParkName(_currentParkName, true);
        _display.displayRide(_rides[_currentRideIndex], _currentRideIndex);
      } else {
        _display.showNoData(NoDataReason::FETCH_FAILED);
      }
      updateLed();
      break;
    case SystemState::NO_PARKS_CONFIGURED:
      _display.showNoData(NoDataReason::NO_PARKS);
      break;
    case SystemState::RECONNECTING:
      _display.showNoData(NoDataReason::WIFI_LOST);
      break;
    case SystemState::STARTUP_INFO:
      _startupScreenShown = false;   // tickStartupInfo repaints it
      break;
    default:
      break;   // WIFI_CONNECTING repaints itself every 500 ms
  }
}

// ------------------------------------------------------------------
// Brightness / quiet hours / status LED
// ------------------------------------------------------------------

uint8_t AppStateManager::effectiveBrightness() const {
  const RuntimeConfig& cfg = _cfg.getConfig();
  if (cfg.quietHoursEnabled) {
    // Quiet hours run on the DEVICE's home timezone when the user picked one;
    // otherwise on the displayed park's clock (the pre-existing behaviour).
    // Fail bright: before NTP sync (or with an unknown zone) we can't know
    // the local time, so never dim based on a guess.
    int nowMin;
    bool haveTime = cfg.deviceTimezone.length() > 0
        ? getMinutesOfDayInTz(cfg.deviceTimezone, nowMin)
        : getLocalMinutesOfDay(nowMin);
    if (haveTime && inQuietWindow(nowMin, cfg.quietStartMin, cfg.quietEndMin))
      return cfg.quietBrightness;
  }
  return cfg.brightness;
}

void AppStateManager::applyBrightness(bool force) {
  uint8_t target = effectiveBrightness();
  if (!force && target == _lastAppliedBrightness) return;
  _lastAppliedBrightness = target;
  LCD_SetBacklight(target);
  updateLed();   // the LED brightness tracks the backlight
}

// Rotate the panel 180° when the user mounted the device upside-down. The
// MADCTL change only affects how NEW pixels are addressed, so the whole
// screen is invalidated to repaint everything into the new orientation.
void AppStateManager::applyScreenFlip() {
  bool flip = _cfg.getConfig().flipScreen;
  if (flip == _lastAppliedFlip) return;
  _lastAppliedFlip = flip;
  LCD_SetRotation(flip);
  lv_obj_invalidate(lv_scr_act());
}

// Reflect the currently shown ride (or closed-park state) on the RGB LED.
// Outside the wait-time cycle the LED stays dark (see transitionTo).
void AppStateManager::updateLed() {
  if (!_cfg.getConfig().ledEnabled ||
      _state != SystemState::WAIT_TIME_CYCLE || _rideCount <= 0) {
    _led.off();
    return;
  }
  WaitLevel level = _showingClosedPark || allRidesClosed()
      ? WaitLevel::Closed
      : pickWaitLevel(_rides[_currentRideIndex].waitTime,
                      _rides[_currentRideIndex].isOpen);
  _led.showLevel(level, _lastAppliedBrightness == 255 ? _cfg.getConfig().brightness
                                                      : _lastAppliedBrightness);
}
