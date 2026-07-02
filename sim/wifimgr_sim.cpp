// WiFiManager sim — mirrors the real credential flow as closely as possible.
//
// Credentials persist in sim_prefs_queuewatch.txt exactly like on-device NVS:
//   - first launch (or after a factory reset) enters the captive portal,
//   - later launches load the saved credentials and connect automatically,
//   - failed connects never wipe them (same rule as src/wifimgr.cpp).
//
// The blocking captive portal is driven by a keystroke instead of a phone:
// press C to "submit the portal form", which saves SimNetwork credentials.
#include "wifimgr.h"
#include "WiFi.h"
#include "sim_events.h"
#include <lvgl.h>
#include <cstdio>
#include <cstdlib>

WiFiManager::WiFiManager() : _configured(false) {}

void WiFiManager::loadCredentials() {
  _prefs.begin("queuewatch", true);
  _ssid = _prefs.getString("ssid", "");
  _pass = _prefs.getString("pass", "");
  _configured = (_ssid.length() > 0);
  _prefs.end();
}

void WiFiManager::saveCredentials(const String& ssid, const String& pass) {
  _prefs.begin("queuewatch", false);
  _prefs.putString("ssid", ssid);
  _prefs.putString("pass", pass);
  _prefs.end();
  _ssid = ssid; _pass = pass; _configured = true;
}

void WiFiManager::clearCredentials() {
  _prefs.begin("queuewatch", false);
  _prefs.remove("ssid");
  _prefs.remove("pass");
  _prefs.end();
  _ssid = ""; _pass = ""; _configured = false;
}

// Blocks like the device portal and exits only after a new save (the QR/portal
// screen was already painted by showCaptivePortalInfo before this is called).
// The loop must pump SDL + LVGL itself: nothing else runs while it blocks.
void WiFiManager::runCaptivePortal() {
  printf("[sim] Captive portal — press C to join WiFi (simulates the portal form)\n");
  _portalSaved = false;
  while (!_portalSaved) {
    if (!sim_pump_events()) std::exit(0);   // window closed / ESC
    if (sim_take_key() == 'c') {
      WiFi.simSetNetwork(true);   // joining a network implies it's in range
      saveCredentials("SimNetwork", "simpass123");
      _portalSaved = true;
    }
    lv_tick_inc(10);
    lv_timer_handler();
    delay(10);
  }
  printf("[sim] Credentials saved — connecting\n");
}

bool WiFiManager::connect() {
  if (!_configured) return false;
  if (WiFi.status() == WL_CONNECTED) { _connecting = false; return true; }
  if (!_connecting) {
    WiFi.begin(_ssid.c_str(), _pass.c_str());   // 1.5 s simulated join
    _connecting = true;
  }
  return false;
}

void WiFiManager::resetConnecting() {
  _connecting = false;
}

void WiFiManager::startAP()         {}
void WiFiManager::startDNSServer()  {}
void WiFiManager::startHTTPServer() {}
void WiFiManager::stopServers()     {}
