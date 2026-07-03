#include "wifimgr.h"
#include "config.h"
#include <WiFi.h>

#if __has_include(<esp_task_wdt.h>)
#include <esp_task_wdt.h>
// The Arduino loopTask is not subscribed to the task watchdog on the C6, so a
// blind esp_task_wdt_reset() logs "task_wdt: task not found" on every call.
// Only feed it when the current task is actually subscribed.
static inline void feedWdtIfSubscribed() {
  if (esp_task_wdt_status(NULL) == ESP_OK) esp_task_wdt_reset();
}
#define FEED_WDT() feedWdtIfSubscribed()
#else
#define FEED_WDT() ((void)0)
#endif

static const char* AP_SSID = "QueueWatch-Config";
static const char* AP_PASS = "config123";
static const byte  DNS_PORT = 53;

// Escape text before it is interpolated into the portal HTML. Nearby AP SSIDs
// (and submitted values) are attacker-controllable and can contain <, >, ", '
// or & — without escaping they break the page or inject markup.
static String htmlEscape(const String& in) {
  String out;
  out.reserve(in.length() + 8);
  for (size_t i = 0; i < in.length(); i++) {
    char c = in[i];
    switch (c) {
      case '&': out += "&amp;";  break;
      case '<': out += "&lt;";   break;
      case '>': out += "&gt;";   break;
      case '"': out += "&quot;"; break;
      case '\'': out += "&#39;"; break;
      default:  out += c;        break;
    }
  }
  return out;
}

static String buildConfigPage(const String& msg, const String& storedSsid) {
  String page = R"(
<html><head><meta name='viewport' content='width=device-width,initial-scale=1'>
<style>
*{box-sizing:border-box;margin:0;padding:0;font-family:-apple-system,sans-serif;}
body{background:#1a1a2e;display:flex;justify-content:center;align-items:center;min-height:100vh;margin:0;}
.card{background:#16213e;border-radius:16px;padding:2rem;width:90%;max-width:400px;box-shadow:0 8px 32px rgba(0,0,0,0.4);}
h1{color:#e94560;font-size:1.5rem;margin-bottom:0.5rem;text-align:center;}
p{color:#a0a0b0;font-size:0.9rem;margin-bottom:1.5rem;text-align:center;}
label{color:#ccc;font-size:0.85rem;display:block;margin-bottom:0.25rem;}
input[type=text],input[type=password]{width:100%;padding:12px;margin-bottom:1rem;border:1px solid #333;border-radius:8px;background:#0f3460;color:#fff;font-size:1rem;outline:none;}
input[type=text]:focus,input[type=password]:focus{border-color:#e94560;}
.btn{width:100%;padding:14px;background:#e94560;color:#fff;border:none;border-radius:8px;font-size:1.1rem;cursor:pointer;transition:opacity 0.2s;}
.btn:hover{opacity:0.85;}
.msg{text-align:center;color:#4ecca3;margin-bottom:1rem;font-size:0.9rem;}
.err{color:#e94560;}
</style></head><body>
<div class='card'>
<h1>&#127979; QueueWatch</h1>
<p>Enter your WiFi credentials to continue</p>
)";
  if (msg.length() > 0) {
    String cssClass = (msg.indexOf("Saved") >= 0) ? "msg" : "msg err";
    page += "<div class='" + cssClass + "'>" + htmlEscape(msg) + "</div>";
  }
  // Reassure the user that the previously working network is not lost: it
  // stays stored until they submit a new one here.
  if (storedSsid.length() > 0) {
    page += "<div class='msg' style='color:#a0a0b0'>Saved network: <strong>" +
            htmlEscape(storedSsid) + "</strong><br>It is kept until you save a new one.</div>";
  }
  page += R"(
<form action='/save' method='POST'>
<label for='ssid'>WiFi Name (SSID)</label>
<input type='text' id='ssid' name='ssid' placeholder='Enter network name' required>
<label for='pass'>Password</label>
<input type='password' id='pass' name='pass' placeholder='Enter password'>
<button class='btn' type='submit'>Connect</button>
</form>
</div></body></html>
)";
  return page;
}

static String buildSuccessPage(const String& ssid) {
  return String(R"(
<html><head><meta name='viewport' content='width=device-width,initial-scale=1'>
<style>*{margin:0;padding:0;font-family:-apple-system,sans-serif;}
body{background:#1a1a2e;display:flex;justify-content:center;align-items:center;min-height:100vh;}
.card{background:#16213e;border-radius:16px;padding:2rem;width:90%;max-width:400px;text-align:center;}
h1{color:#4ecca3;font-size:1.5rem;margin-bottom:1rem;}
p{color:#a0a0b0;margin-bottom:1rem;}
</style></head><body><div class='card'>
<h1>&#10003; Saved!</h1>
<p>Attempting to connect to <strong>)") + htmlEscape(ssid) + R"(</strong>&hellip;</p>
<p>If the wrong password was entered, reconnect to the <strong>QueueWatch-Config</strong>
Wi-Fi network to try again.</p>
</div></body></html>)";
}

WiFiManager::WiFiManager() : _configured(false), _webServer(80) {}

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
  _prefs.remove("ssid"); _prefs.remove("pass");
  _prefs.end();
  _ssid = ""; _pass = ""; _configured = false;
}

void WiFiManager::startAP() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  delay(200);
}

void WiFiManager::startDNSServer() {
  _dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
}

void WiFiManager::startHTTPServer() {
  _webServer.on("/", [this]() {
    _webServer.send(200, "text/html", buildConfigPage("", _ssid));
  });
  _webServer.on("/save", HTTP_POST, [this]() {
    String ssid = _webServer.arg("ssid");
    String pass = _webServer.arg("pass");
    if (ssid.length() == 0) {
      _webServer.send(200, "text/html", buildConfigPage("SSID cannot be empty", _ssid));
      return;
    }
    _webServer.send(200, "text/html", buildSuccessPage(ssid));
    saveCredentials(ssid, pass);
    _portalSaved = true;
  });
  _webServer.onNotFound([this]() {
    _webServer.sendHeader("Location",
      String("http://") + _webServer.client().localIP().toString(), true);
    _webServer.send(302, "text/plain", "");
  });
  _webServer.begin();
}

void WiFiManager::stopServers() {
  _dnsServer.stop();
  _webServer.close();
}

// Blocks until the user saves credentials in the portal. Exits only on a
// *new* save (_portalSaved), not on _configured: stored credentials survive
// failed connects, so the portal may be entered while old ones still exist —
// e.g. the device was powered on away from its home network. Those must be
// kept so a power cycle back home just works.
void WiFiManager::runCaptivePortal() {
  _portalSaved = false;
  startAP();
  startDNSServer();
  startHTTPServer();
  while (true) {
    _dnsServer.processNextRequest();
    _webServer.handleClient();
    FEED_WDT();
    delay(10);
    if (_portalSaved) break;
  }
  stopServers();
  WiFi.mode(WIFI_STA);
  delay(200);
}

bool WiFiManager::connect() {
  if (!_configured) return false;
  if (WiFi.status() == WL_CONNECTED) { _connecting = false; return true; }
  if (!_connecting) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(_ssid.c_str(), _pass.c_str());
    _connecting = true;
  }
  return false;
}

void WiFiManager::resetConnecting() {
  _connecting = false;
}
