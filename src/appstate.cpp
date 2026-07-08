#include <WiFi.h>
#include "appstate.h"
#include "tzhelper.h"
#include "quiethours.h"
#include "ridefilter.h"
#include "ridereindex.h"
#include "idhash.h"
#include "waitlevel.h"
#include "lcd_st7789.h"

// How often the quiet-hours window / brightness setting is re-evaluated.
static constexpr unsigned long BRIGHTNESS_CHECK_INTERVAL = 10000UL;

// Reconnect attempt gives up (and counts as a failure) after this long.
static constexpr unsigned long WIFI_ATTEMPT_TIMEOUT_MS = 15000UL;
// Consecutive reconnect failures before showing the WIFI_TROUBLE screen.
static constexpr int WIFI_TROUBLE_FAIL_COUNT = 3;
// Consecutive park load failures (fetch error or zero usable rides) before
// moving on to the next configured park instead of retrying the same one.
static constexpr int MAX_PARK_LOAD_FAILURES = 2;
// "Connecting..." dot animation cadence and frame count (0..3, then wraps).
static constexpr unsigned long CONNECT_DOT_ANIM_MS = 500UL;
static constexpr int CONNECT_DOT_FRAME_MASK = 3;
// Minimum backlight during the factory-reset warning, so it's readable even
// if quiet hours currently has the backlight near zero.
static constexpr uint8_t RESET_WARNING_MIN_BRIGHTNESS = 30;
// How long showFactoryResetting() stays up before the actual wipe + restart.
static constexpr unsigned long FACTORY_RESET_DISPLAY_MS = 600UL;
// How long showOtaInstalling() stays up before Update.end() + restart.
static constexpr unsigned long OTA_INSTALL_DISPLAY_MS = 600UL;
// Consecutive successful wait-time fetches after an OTA update before
// canceling the ESP-IDF app-rollback safety net (see OtaUpdater).
static constexpr int OTA_CONFIRM_FETCH_COUNT = 3;
// --- Fetch-failure self-healing -----------------------------------------
// Fetches can keep failing while WiFi.status() still reports WL_CONNECTED:
// a "zombie" association (router rebooted, lease lost) or a heap too
// fragmented for another ~45 KB TLS session. Neither ever recovers on its
// own, so escalate: after this many consecutive failures force a full WiFi
// reconnect cycle (fixes the zombie case)...
static constexpr int FETCH_FAIL_WIFI_CYCLE_STREAK = 4;
// ...and if no fetch has succeeded for this long despite that, reboot.
// All config lives in NVS, so a restart costs ~30 s and beats being stuck
// on "No Ride Data" until someone pulls the plug.
static constexpr unsigned long FETCH_FAIL_REBOOT_MS = 15UL * 60UL * 1000UL;
// A fresh TLS handshake needs roughly 45 KB of free heap; below this every
// future fetch is guaranteed to fail, so restart while we still can.
static constexpr uint32_t MIN_FREE_HEAP_BYTES = 40000;
// While the schedule says the park is closed, /live polling is suppressed —
// but a wrong or changed schedule must self-correct, so still re-check
// (schedule + live) this often instead of at the API refresh interval.
static constexpr unsigned long CLOSED_PARK_SANITY_POLL_MS = 60UL * 60UL * 1000UL;

// "HH:MM" for a minutes-of-day value (park hours in the header / sub-label).
static String formatHHMM(int minutes) {
  char buf[6];
  snprintf(buf, sizeof(buf), "%02d:%02d", minutes / 60, minutes % 60);
  return String(buf);
}

AppStateManager* AppStateManager::_instance = nullptr;

AppStateManager::AppStateManager(WiFiManager& wifi, QueueApi& api,
                                  ConfigManager& cfg, DisplayController& display,
                                  ConfigWebServer& webServer, StatusLed& led)
  : _wifi(wifi), _api(api), _cfg(cfg),
    _display(display), _webServer(webServer), _led(led) {
  _instance = this;
}

void AppStateManager::begin() {
  reloadRuntimeConfig();
  applyBrightness(true);   // LCD_Init leaves the backlight at 100%
  applyScreenFlip();       // ...and the panel unflipped
  applyColorPalette();     // screens are built in the default palette
  applyWaitConfig();       // ...and with the default wait thresholds/colours
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
    // Low-heap watchdog: once free heap can no longer hold a TLS session,
    // the device is unrecoverable without a reboot — do it proactively.
    uint32_t freeHeap = ESP.getFreeHeap();
    if (!_resetWarningActive && freeHeap < MIN_FREE_HEAP_BYTES) {
      Serial.printf("[recovery] free heap critically low (%u bytes) - restarting\n",
                    (unsigned)freeHeap);
      delay(100);  // let the log line flush
      ESP.restart();
    }
  }

  // The factory-reset warning freezes everything: no rotation, no refresh,
  // no screen changes until the button is released (cancel) or held to 20 s.
  if (_resetWarningActive) return;

  if (_webServer.isConfigUpdated()) {
    _webServer.clearConfigFlag();
    applyScreenFlip();       // before the repaint restartCycle triggers
    applyColorPalette();
    applyWaitConfig();
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
  _wifiTrouble = false;
  _display.showConnectingScreen(0);
}

void AppStateManager::tickWifiConnecting(unsigned long now) {
  if (!_wifiTrouble && now - _wifiConnectStart >= WIFI_CONNECT_TIMEOUT_MS) {
    // Never fall back to the config portal here: the network may simply be
    // down or out of range, and the BOOT button (20 s hold) covers the case
    // where the credentials really are wrong. Tell the user what's going on
    // and keep retrying forever.
    enterWifiTroubleScreen();
    _wifi.resetConnecting();
    _lastWiFiTry = now;
  }

  if (_wifiTrouble) {
    // Start a fresh connection attempt every WIFI_RECONNECT_INTERVAL.
    if (now - _lastWiFiTry >= WIFI_RECONNECT_INTERVAL) {
      _lastWiFiTry = now;
      _wifi.resetConnecting();
    }
  } else if (now - _lastDotUpdate >= CONNECT_DOT_ANIM_MS) {
    _connectingDotCount = (_connectingDotCount + 1) & CONNECT_DOT_FRAME_MASK;
    _display.showConnectingScreen(_connectingDotCount);
    _lastDotUpdate = now;
  }

  _wifi.connect();
  if (WiFi.status() == WL_CONNECTED) {
    _wifiTrouble = false;
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

  // Web-UI "Install update" click. The version/asset-URL check already
  // happened synchronously inside ConfigWebServer::handleApiOtaCheck(), so
  // by the time this flag is set the asset to download is already known.
  if (_webServer.consumeOtaStartRequest(_otaAssetUrl)) {
    transitionTo(SystemState::OTA_DOWNLOADING);
    return;
  }

  const RuntimeConfig& cfg = _cfg.getConfig();
  if (cfg.enabledParkIds.size() == 0) {
    _display.showNoData(NoDataReason::NO_PARKS);
    return;
  }

  // Fetch-failure escalation (see the FETCH_FAIL_* constants). WiFi says
  // connected (checked above) yet fetches keep failing: first force one
  // full reconnect cycle per streak, and if the streak still isn't broken
  // after 15 minutes, reboot — nothing else recovers an exhausted heap.
  if (_fetchFailStreak >= FETCH_FAIL_WIFI_CYCLE_STREAK && !_wifiCycleTried) {
    _wifiCycleTried = true;
    _fetchFailStreakAtCycle = _fetchFailStreak;
    Serial.printf("[recovery] %d consecutive fetch failures (heap %u) - cycling WiFi\n",
                  _fetchFailStreak, (unsigned)ESP.getFreeHeap());
    WiFi.disconnect();
    _wifi.resetConnecting();
    transitionTo(SystemState::RECONNECTING);
    return;
  }
  // Reboot only after the reconnect cycle was tried AND at least one more
  // fetch failed after it — a lone transient refresh failure (they're 15 min
  // apart when stale data is still on screen) must never trigger a restart.
  if (_wifiCycleTried && _fetchFailStreak > _fetchFailStreakAtCycle &&
      now - _fetchFailSince >= FETCH_FAIL_REBOOT_MS) {
    Serial.printf("[recovery] no successful fetch for %lu min - restarting\n",
                  (now - _fetchFailSince) / 60000UL);
    delay(100);  // let the log line flush
    ESP.restart();
  }

  // Schedule check: outside today's operating window the park is closed no
  // matter what /live says (or whether it could even be fetched). Hours are
  // per-park, date-stamped, and refreshed by loadParkData().
  int  nowMin = 0;
  bool scheduleClosed = getLocalMinutesOfDay(nowMin) &&
                        parkClosedNow(_parkHours, nowMin);

  // The park just (re)opened while its closed screen was up — the schedule
  // suppression above kept the data stale all night, so reload immediately
  // instead of waiting out a poll interval.
  if (_closedBySchedule && !scheduleClosed) {
    _closedBySchedule = false;
    _lastApiFetch = now;
    loadParkData();
    return;
  }

  if (_rideCount <= 0 && !scheduleClosed) {
    if (now - _lastApiFetch >= DATA_RETRY_INTERVAL) {
      _lastApiFetch = now;
      // A park that keeps failing (fetch error or zero usable rides) must not
      // stall the whole cycle: after two failed retries move on to the next
      // configured park instead of hammering the same one forever.
      if (_parkLoadFailures >= MAX_PARK_LOAD_FAILURES && cfg.enabledParkIds.size() > 1) {
        advanceToNextPark();
      } else {
        loadParkData();
      }
    }
    return;
  }

  if (scheduleClosed || allRidesClosed()) {
    _closedBySchedule = scheduleClosed;
    if (!_showingClosedPark) {
      showClosedParkScreen(nowMin);
      _showingClosedPark = true;
      _closedParkStart = now;
      updateLed();
    } else if (now - _closedParkStart >= _cfgClosedParkDisplayTime) {
      if (cfg.enabledParkIds.size() > 1) {
        advanceToNextPark();
      } else {
        // Single park, closed: nothing to rotate to, so only re-check at a
        // slow cadence instead of every closed-park interval (~20 s) — all-
        // night TLS churn fragments the heap and hammers the API. With known
        // hours an hourly sanity poll (schedule could be wrong or change) is
        // enough — reopening is caught by the schedule check above, not by
        // polling; without hours fall back to the API refresh interval.
        unsigned long recheck = scheduleClosed ? CLOSED_PARK_SANITY_POLL_MS
                                               : _cfgApiRefreshInterval;
        if (now - _lastApiFetch >= recheck) {
          _lastApiFetch = now;
          loadParkData();
        }
      }
    }
    if (now - _lastTimeUpdate >= _cfgTimeUpdateInterval) {
      showClosedParkScreen(nowMin);
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
  int        prevCount = _rideCount;
  String     prevId[MAX_RIDES];
  int        prevWait[MAX_RIDES];
  RideStatus prevStatus[MAX_RIDES];
  int16_t    prevShow[MAX_RIDES];
  for (int i = 0; i < prevCount; i++) {
    prevId[i]     = _rides[i].id;
    prevWait[i]   = _rides[i].waitTime;
    prevStatus[i] = _rides[i].status;
    prevShow[i]   = _rides[i].nextShowMin;
  }
  if (!fetchAndProcessRideData()) return;

  // Wait-desc sorting can reorder rides between refreshes, so the same index
  // may now point at a different ride — re-find the ride the user was
  // actually looking at by id instead of just clamping the old index.
  _currentRideIndex = reindexAfterRefresh(prevId, prevCount, _currentRideIndex,
                                           _rides, _rideCount);

  // NOTE: wait-desc sorting can reorder rides between refreshes; the name
  // diff below detects that as a structure change and repaints fully.
  bool structureChanged = (_rideCount != prevCount);
  bool waitsChanged     = false;
  for (int i = 0; !structureChanged && i < _rideCount; i++) {
    if (_rides[i].id != prevId[i] || _rides[i].name != _lastRideNames[i])
      structureChanged = true;
    else if (_rides[i].waitTime != prevWait[i] || _rides[i].status != prevStatus[i] ||
             _rides[i].nextShowMin != prevShow[i] || _rides[i].trend != 0)
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
  _wifiTrouble = false;
  // Stamp the attempt-timeout clock here (state entry) rather than lazily
  // inside tickReconnecting: if connect() ever returned true on the very
  // first reconnect tick without WL_CONNECTED, the lazy stamp would never
  // happen and the 15 s timeout below could never fire.
  _wifiBeginTime = millis();
  _display.showNoData(NoDataReason::WIFI_LOST);
}

// ==================================================================
// NO_PARKS_CONFIGURED
// ==================================================================

void AppStateManager::enterNoParksConfigured() {
  // Reached from the startup splash when no parks are set. Without this the
  // stale splash would stay on screen (the tick handler is a no-op).
  _display.showNoData(NoDataReason::NO_PARKS);
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
    _wifiTrouble = false;
    _wifi.resetConnecting();
    syncParkTimezone(true);  // force: re-sync NTP after the outage
    resetCycleTimers();
    _webServer.begin();
    transitionTo(SystemState::WAIT_TIME_CYCLE);
    return;
  }

  bool attemptTimedOut   = (_wifiBeginTime > 0 && (now - _wifiBeginTime >= WIFI_ATTEMPT_TIMEOUT_MS));
  bool terminalFailure   = (WiFi.status() == WL_CONNECT_FAILED ||
                            WiFi.status() == WL_NO_SSID_AVAIL);
  bool disconnectedAfter = (WiFi.status() == WL_DISCONNECTED && attemptTimedOut);

  if (terminalFailure || disconnectedAfter) {
    _wifiFailCount++;
    _wifi.resetConnecting();
    _wifiBeginTime = 0;
    if (_wifiFailCount >= WIFI_TROUBLE_FAIL_COUNT && !_wifiTrouble) {
      // Repeated failures no longer open the config portal (the BOOT button
      // handles a real reconfiguration): explain how to recover on-screen
      // and keep retrying — the router may just be rebooting.
      enterWifiTroubleScreen();
    }
  }
}

// Shared by WIFI_CONNECTING and RECONNECTING: both eventually give up
// waiting and show the same "still trying" screen with the same recovery
// hint (see the callers' comments for why neither falls back to the portal).
void AppStateManager::enterWifiTroubleScreen() {
  _wifiTrouble = true;
  _display.showNoData(NoDataReason::WIFI_TROUBLE);
}

// ==================================================================
// OTA_DOWNLOADING / OTA_FLASHING
// ==================================================================

// Static trampoline for OtaUpdater's plain-function-pointer progress
// callback (it can't capture `this`) — see the _instance comment in
// appstate.h for why a single static pointer is safe here.
void AppStateManager::otaProgressThunk(size_t written, size_t total) {
  if (!_instance) return;
  uint8_t pct = (total > 0) ? (uint8_t)((written * 100) / total) : 0;
  _instance->_display.showOtaDownloading(pct);
  _instance->_webServer.setOtaStatus(OtaUiState::Downloading, pct);
}

// Like the captive portal (enterWifiConfigPortal()), this blocks fully
// inside itself — performUpdate() is one long synchronous call, matching
// every other HTTP operation in this codebase — and transitions onward
// before returning, so update()'s switch never actually reaches a tick
// handler for this state.
void AppStateManager::enterOtaDownloading() {
  _display.showOtaDownloading(0);
  _webServer.setOtaStatus(OtaUiState::Downloading, 0);

  OtaResult result = _ota.performUpdate(_otaAssetUrl, &AppStateManager::otaProgressThunk);

  if (result == OtaResult::Success) {
    transitionTo(SystemState::OTA_FLASHING);
    return;
  }

  const char* msg = "Update failed";
  switch (result) {
    case OtaResult::ErrorNotEnoughSpace: msg = "Not enough space for update"; break;
    case OtaResult::ErrorDownloadFailed: msg = "Download failed"; break;
    case OtaResult::ErrorFlashFailed:    msg = "Flash verification failed"; break;
    case OtaResult::ErrorNotSupported:   msg = "OTA not supported"; break;
    default: break;
  }
  _webServer.setOtaStatus(OtaUiState::Error, 0, msg);
  // Never touches the currently-running image on failure (see performUpdate()'s
  // comment) — just resume normal ride-cycling, repainted fresh.
  transitionTo(SystemState::WAIT_TIME_CYCLE);
  restartCycle();
}

void AppStateManager::enterOtaFlashing() {
  _display.showOtaInstalling();
  _webServer.setOtaStatus(OtaUiState::Installing, 100);
  delay(OTA_INSTALL_DISPLAY_MS);   // long enough to read before the panel goes dark
  ESP.restart();
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
    case SystemState::WIFI_CONFIG_PORTAL:  enterWifiConfigPortal();   break;
    case SystemState::WIFI_CONNECTING:     enterWifiConnecting();     break;
    case SystemState::RECONNECTING:        enterReconnecting();       break;
    case SystemState::NO_PARKS_CONFIGURED: enterNoParksConfigured();  break;
    case SystemState::OTA_DOWNLOADING:     enterOtaDownloading();     break;
    case SystemState::OTA_FLASHING:        enterOtaFlashing();        break;
    default: break;
  }
}

// ==================================================================
// Helpers
// ==================================================================

// "Closed" here means nothing worth showing: no attraction operating or
// down (a breakdown implies the park itself is open), and no show with a
// remaining showtime today.
bool AppStateManager::allRidesClosed() const {
  if (_rideCount <= 0) return true;
  for (int i = 0; i < _rideCount; i++) {
    const RideInfo& r = _rides[i];
    if (r.kind == EntityKind::Show) {
      if (r.nextShowMin >= 0) return false;
    } else if (r.isOpen() || r.status == RideStatus::Down) {
      return false;
    }
  }
  return true;
}

// Closed-park screen with the most helpful sub-text the schedule allows:
// "OPENS 09:00" before today's opening, "CLOSED TODAY" on a no-hours day,
// otherwise the generic "PARK IS CLOSED TODAY" (after close / no schedule).
void AppStateManager::showClosedParkScreen(int nowMin) {
  String sub;
  if (_parkHours.closedAllDay()) {
    sub = "CLOSED TODAY";
  } else if (_parkHours.known() && nowMin < _parkHours.openMin) {
    sub = "OPENS " + formatHHMM(_parkHours.openMin);
  }
  _display.showClosedPark(_currentParkName, sub);
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
    // TrendStore keys by int32 — hash the UUID (see idhash.h).
    int delta = _trends.updateAndGetDelta((int32_t)fnv1a32(r.id.c_str()),
                                          r.isOpen() ? r.waitTime : -1);
    r.trend      = (delta > 0) ? 1 : (delta < 0) ? -1 : 0;
    r.trendDelta = (int16_t)delta;
    r.favorite   = _cfg.isRideFavorite(_currentParkId, r.id);
  }
}

// Fetch the current park's rides and run them through the shared
// filter/annotate/sort pipeline, updating the display's ride-count and
// freshness metadata. Shared by loadParkData() and refreshRideData() so the
// two callers can't drift on what "process the fetched rides" means.
bool AppStateManager::fetchAndProcessRideData() {
  // Next-showtime selection needs the park-local date + time of day. The
  // park's timezone is already applied (syncParkTimezone), so the device
  // clock IS park-local. Before NTP sync: empty date + minute 0 — shows then
  // simply carry no "next showtime", which corrects on the next refresh.
  int nowMin = 0;
  if (!getLocalMinutesOfDay(nowMin)) nowMin = 0;
  // Fetch into a scratch array: the streaming parser writes elements as they
  // arrive, so a mid-body failure would otherwise leave _rides half-updated
  // — refreshRideData() relies on stale-but-consistent data surviving a
  // failed refresh. Static: ~5 KB is too big for the loop task's stack.
  static RideInfo scratch[MAX_RIDES];
  int scratchCount = 0;
  if (!_api.fetchRideData(_currentParkId, getLocalDateString(), nowMin,
                          scratch, scratchCount, MAX_RIDES)) {
    if (_fetchFailStreak == 0) _fetchFailSince = millis();
    _fetchFailStreak++;
    return false;
  }
  for (int i = 0; i < scratchCount; i++) _rides[i] = std::move(scratch[i]);
  _rideCount = scratchCount;
  _fetchFailStreak = 0;
  _wifiCycleTried  = false;
  applyRideFilter();
  annotateRides();
  applyRideDisplayOptions();
  _display.setRideCount(_rideCount);
  _display.setDataFreshness(0);

  // Post-OTA rollback confirmation: once enough successful fetches have
  // happened since boot, cancel the ESP-IDF app-rollback safety net so this
  // image is considered good (see OtaUpdater::markBootSuccessful()). A
  // no-op fast path once already confirmed or when nothing is pending.
  if (!_otaConfirmed) {
    if (!OtaUpdater::isPendingConfirmation()) {
      _otaConfirmed = true;
    } else if (++_otaConfirmFetchCount >= OTA_CONFIRM_FETCH_COUNT) {
      OtaUpdater::markBootSuccessful();
      _otaConfirmed = true;
    }
  }
  return true;
}

// Full load of the current park: sync its timezone, fetch rides, and paint
// the appropriate screen from scratch.
void AppStateManager::loadParkData() {
  syncParkTimezone();

  // Today's operating hours: drive the header label above the clock and the
  // schedule-based closed-park handling in tickWaitTimeCycle(). Cached per
  // park per day inside QueueApi, so this is one fetch per park per day. A
  // failed fetch resets to "unknown" — closed detection then falls back to
  // the all-rides-closed inference.
  _parkHours = ParkHours();
  _closedBySchedule = false;  // re-derived from the fresh hours next tick
  _api.getParkHours(_currentParkId, getLocalDateString(), _parkHours);
  if (_parkHours.known()) {
    _display.setHeaderInfo(formatHHMM(_parkHours.openMin) + "-" +
                           formatHHMM(_parkHours.closeMin == 1440
                                          ? 0 : _parkHours.closeMin));
  } else {
    _display.setHeaderInfo("");
  }

  if (fetchAndProcessRideData()) {
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
      if (brt < RESET_WARNING_MIN_BRIGHTNESS) brt = RESET_WARNING_MIN_BRIGHTNESS;
      LCD_SetBacklight(brt);
      _lastAppliedBrightness = brt;
      _brightnessApplied = true;
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
      delay(FACTORY_RESET_DISPLAY_MS);   // long enough to read before the panel goes dark
      _cfg.factoryReset();
      ESP.restart();
      return;
    default:
      break;
  }

  // A press during the 10 s startup splash skips the rest of it: backdate the
  // state-enter clock so tickStartupInfo() runs its normal transition (into the
  // ride cycle, or the no-parks screen) on this same loop iteration.
  if (_state == SystemState::STARTUP_INFO &&
      (ev == ButtonEvent::Short || ev == ButtonEvent::Long)) {
    _stateEnterTime = millis() - STARTUP_SPLASH_DURATION;
    return;
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
      _display.showNoData(_wifiTrouble ? NoDataReason::WIFI_TROUBLE
                                       : NoDataReason::WIFI_LOST);
      break;
    case SystemState::STARTUP_INFO:
      _startupScreenShown = false;   // tickStartupInfo repaints it
      break;
    case SystemState::WIFI_CONNECTING:
      // The dot animation repaints itself; the trouble screen does not.
      if (_wifiTrouble) _display.showNoData(NoDataReason::WIFI_TROUBLE);
      break;
    default:
      break;
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
  if (!force && _brightnessApplied && target == _lastAppliedBrightness) return;
  _lastAppliedBrightness = target;
  _brightnessApplied = true;
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

// Restyle the LVGL screens when the user picked a different UI palette.
void AppStateManager::applyColorPalette() {
  const RuntimeConfig& cfg = _cfg.getConfig();
  // Keep the Custom palette definition current — its picked colours can change
  // while the selected index stays put, so push them every time.
  _display.setCustomPalette(cfg.customHdr, cfg.customAccent, cfg.customPanel);
  uint8_t pal = cfg.colorPalette;
  bool isCustom = (pal == COLOR_PALETTE_COUNT - 1);
  // Re-apply whenever the index changed, or it's Custom (colours may have moved).
  if (pal == _lastAppliedPalette && !isCustom) return;
  _lastAppliedPalette = pal;
  _display.applyPalette(pal);
}

// Push the user-configured wait thresholds + level colours to the display
// themes and the LED (they must always agree).
void AppStateManager::applyWaitConfig() {
  const RuntimeConfig& cfg = _cfg.getConfig();
  _display.setWaitConfig(cfg.waitTh1, cfg.waitTh2, cfg.waitTh3, cfg.waitColors);
  _led.setColors(cfg.waitColors);
}

// Reflect the currently shown ride (or closed-park state) on the RGB LED.
// Outside the wait-time cycle the LED stays dark (see transitionTo).
void AppStateManager::updateLed() {
  const RuntimeConfig& cfg = _cfg.getConfig();
  if (!cfg.ledEnabled ||
      _state != SystemState::WAIT_TIME_CYCLE || _rideCount <= 0) {
    _led.off();
    return;
  }
  WaitLevel level = _showingClosedPark || allRidesClosed()
      ? WaitLevel::Closed
      : pickWaitLevel(_rides[_currentRideIndex].waitTime,
                      _rides[_currentRideIndex].isOpen(),
                      cfg.waitTh1, cfg.waitTh2, cfg.waitTh3);
  _led.showLevel(level, _brightnessApplied ? _lastAppliedBrightness
                                            : _cfg.getConfig().brightness);
}
