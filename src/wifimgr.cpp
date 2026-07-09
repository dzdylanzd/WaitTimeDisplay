#include "wifimgr.h"
#include "config.h"
#include <WiFi.h>
#include <vector>
#include <algorithm>

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

// Escape a string for embedding inside a JSON string literal (the /scan
// endpoint returns nearby SSIDs, which are attacker-controllable and may
// contain quotes, backslashes or control bytes).
static String jsonEscape(const String& in) {
  String out;
  out.reserve(in.length() + 8);
  for (size_t i = 0; i < in.length(); i++) {
    char c = in[i];
    switch (c) {
      case '\\': out += "\\\\"; break;
      case '"':  out += "\\\""; break;
      case '\n': out += "\\n";  break;
      case '\r': out += "\\r";  break;
      case '\t': out += "\\t";  break;
      default:
        if ((unsigned char)c < 0x20) {
          char b[8]; snprintf(b, sizeof(b), "\\u%04x", (unsigned char)c);
          out += b;
        } else {
          out += c;
        }
    }
  }
  return out;
}

static String buildConfigPage(const String& msg, const String& storedSsid) {
  String page = R"(
<html><head><meta name='viewport' content='width=device-width,initial-scale=1'>
<style>
:root{--bg1:#e9eef7;--bg2:#dde6f4;--card:rgba(255,255,255,.72);--brd:rgba(28,40,80,.14);
 --ink:#182233;--mut:#5c6780;--field:#fff;--fieldbrd:#cdd6e6;--accent:#6a58ee;
 --ok:#12b886;--warn:#c07d0a;--shadow:0 16px 40px rgba(30,50,90,.16);color-scheme:light;}
@media(prefers-color-scheme:dark){:root{--bg1:#0d1330;--bg2:#0a0f24;--card:rgba(26,30,58,.66);
 --brd:rgba(255,255,255,.12);--ink:#eef1fb;--mut:#a6abc8;--field:rgba(255,255,255,.06);
 --fieldbrd:rgba(255,255,255,.17);--accent:#8f7cff;--ok:#3ad9a0;--warn:#f0b34a;
 --shadow:0 18px 44px rgba(0,0,0,.5);color-scheme:dark;}}
*{box-sizing:border-box;margin:0;padding:0;font-family:-apple-system,'Segoe UI',Roboto,sans-serif;}
body{background:radial-gradient(900px 500px at 50% -10%,color-mix(in srgb,var(--accent) 22%,transparent),transparent 60%),linear-gradient(var(--bg1),var(--bg2));display:flex;justify-content:center;align-items:center;min-height:100vh;padding:16px;}
.card{background:var(--card);backdrop-filter:blur(12px);border:1px solid var(--brd);border-radius:20px;padding:1.8rem;width:100%;max-width:400px;box-shadow:var(--shadow);}
h1{color:var(--ink);font-size:1.5rem;margin-bottom:0.4rem;text-align:center;font-weight:800;}
p{color:var(--mut);font-size:0.9rem;margin-bottom:1.4rem;text-align:center;}
label{color:var(--ink);font-size:0.82rem;font-weight:600;display:block;margin-bottom:0.3rem;}
input[type=text],input[type=password],select{width:100%;padding:12px;margin-bottom:1rem;border:1px solid var(--fieldbrd);border-radius:10px;background:var(--field);color:var(--ink);font-size:1rem;outline:none;}
#ssidPick{margin-bottom:0.6rem;}
input[type=text]:focus,input[type=password]:focus,select:focus{border-color:var(--accent);}
.btn{width:100%;padding:14px;background:var(--accent);color:#fff;border:none;border-radius:10px;font-size:1.05rem;font-weight:700;cursor:pointer;transition:filter .2s,transform .1s;}
.btn:hover{filter:brightness(1.08);}
.btn:active{transform:translateY(1px);}
.msg{text-align:center;color:var(--ok);margin-bottom:1rem;font-size:0.9rem;}
.err{color:#e5484d;}
.hint{color:var(--warn);font-size:0.8rem;text-align:center;margin-top:-1rem;margin-bottom:1.4rem;}
.row{display:flex;justify-content:space-between;align-items:center;margin:-0.5rem 0 1rem;}
.small{color:var(--mut);font-size:0.8rem;}
.link{background:none;border:none;color:var(--accent);font-size:0.8rem;cursor:pointer;padding:0;font-weight:600;}
.showpw{display:flex;align-items:center;gap:6px;color:var(--mut);font-size:0.8rem;margin:-0.5rem 0 1rem;cursor:pointer;}
.showpw input{width:auto;margin:0;}
</style></head><body>
<div class='card'>
<h1>&#127979; QueueWatch</h1>
<p>Enter your WiFi credentials to continue</p>
<p class='hint'>&#128246; Only 2.4 GHz Wi-Fi networks are supported.</p>
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
<label for='ssidPick'>WiFi Name (SSID)</label>
<select id='ssidPick' onchange="if(this.value){document.getElementById('ssid').value=this.value;}">
<option value=''>&#128246; Tap &ldquo;Rescan&rdquo; to find networks&hellip;</option>
</select>
<input type='text' id='ssid' name='ssid' placeholder='...or type the network name' autocomplete='off' required>
<div class='row'>
<span id='scanStatus' class='small'></span>
<button type='button' id='scanBtn' onclick='scanWifi()' class='link'>&#128260; Rescan</button>
</div>
<label for='pass'>Password</label>
<input type='password' id='pass' name='pass' placeholder='Enter password'>
<label class='showpw'>
<input type='checkbox' onclick="document.getElementById('pass').type=this.checked?'text':'password';"> Show password</label>
<button class='btn' type='submit'>Connect</button>
</form>
</div>
<script>
async function scanWifi(){
  var btn=document.getElementById('scanBtn'),st=document.getElementById('scanStatus'),
      sel=document.getElementById('ssidPick');
  btn.disabled=true;st.textContent='Scanning...';
  try{
    var res=await fetch('/scan');var nets=await res.json();
    sel.innerHTML='<option value="">-- pick a network --</option>';
    nets.forEach(function(n){
      var o=document.createElement('option');
      o.value=n.ssid;
      o.textContent=(n.secure?'🔒 ':'')+n.ssid+' ('+n.rssi+' dBm)';
      sel.appendChild(o);
    });
    st.textContent=nets.length?(nets.length+' network'+(nets.length>1?'s':'')+' found'):'No networks found';
  }catch(e){st.textContent='Scan failed - type the name instead';}
  btn.disabled=false;
}
window.addEventListener('load',scanWifi);
</script>
</body></html>
)";
  return page;
}

static String buildSuccessPage(const String& ssid) {
  return String(R"(
<html><head><meta name='viewport' content='width=device-width,initial-scale=1'>
<style>:root{--bg1:#e9eef7;--bg2:#dde6f4;--card:rgba(255,255,255,.72);--brd:rgba(28,40,80,.14);--ink:#182233;--mut:#5c6780;--ok:#12b886;--shadow:0 16px 40px rgba(30,50,90,.16);color-scheme:light;}
@media(prefers-color-scheme:dark){:root{--bg1:#0d1330;--bg2:#0a0f24;--card:rgba(26,30,58,.66);--brd:rgba(255,255,255,.12);--ink:#eef1fb;--mut:#a6abc8;--ok:#3ad9a0;--shadow:0 18px 44px rgba(0,0,0,.5);color-scheme:dark;}}
*{margin:0;padding:0;box-sizing:border-box;font-family:-apple-system,'Segoe UI',Roboto,sans-serif;}
body{background:radial-gradient(900px 500px at 50% -10%,color-mix(in srgb,var(--ok) 20%,transparent),transparent 60%),linear-gradient(var(--bg1),var(--bg2));display:flex;justify-content:center;align-items:center;min-height:100vh;padding:16px;}
.card{background:var(--card);backdrop-filter:blur(12px);border:1px solid var(--brd);border-radius:20px;padding:2rem;width:100%;max-width:400px;text-align:center;box-shadow:var(--shadow);}
h1{color:var(--ok);font-size:1.5rem;margin-bottom:1rem;font-weight:800;}
p{color:var(--mut);margin-bottom:1rem;line-height:1.5;}
strong{color:var(--ink);}
</style></head><body><div class='card'>
<h1>&#10003; Saved!</h1>
<p>Attempting to connect to <strong>)") + htmlEscape(ssid) + R"(</strong>&hellip;</p>
<p>If the wrong password was entered, reconnect to the <strong>QueueWatch-Config</strong>
Wi-Fi network to try again.</p>
</div></body></html>)";
}

WiFiManager::WiFiManager() : _configured(false), _webServer(80) {}

void WiFiManager::loadCredentials() {
  _prefs.begin(NVS_NAMESPACE, true);
  _ssid = _prefs.getString("ssid", "");
  _pass = _prefs.getString("pass", "");
  _configured = (_ssid.length() > 0);
  _prefs.end();
}

void WiFiManager::saveCredentials(const String& ssid, const String& pass) {
  _prefs.begin(NVS_NAMESPACE, false);
  _prefs.putString("ssid", ssid);
  _prefs.putString("pass", pass);
  _prefs.end();
  _ssid = ssid; _pass = pass; _configured = true;
}

void WiFiManager::clearCredentials() {
  _prefs.begin(NVS_NAMESPACE, false);
  _prefs.remove("ssid"); _prefs.remove("pass");
  _prefs.end();
  _ssid = ""; _pass = ""; _configured = false;
}

void WiFiManager::startAP() {
  // AP_STA (not plain AP) so the /scan endpoint can run WiFi.scanNetworks()
  // on the STA interface while the SoftAP stays up for the config client.
  WiFi.mode(WIFI_AP_STA);
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
  _webServer.on("/scan", HTTP_GET, [this]() {
    // Blocking scan (~2-4 s) — consistent with this codebase's synchronous-HTTP
    // convention. Returns nearby networks as JSON, de-duplicated by SSID
    // (keeping the strongest signal) and sorted by RSSI descending.
    struct Net { String ssid; int32_t rssi; bool secure; };
    int n = WiFi.scanNetworks();
    std::vector<Net> nets;
    for (int i = 0; i < n; i++) {
      String ssid = WiFi.SSID(i);
      if (ssid.length() == 0) continue;   // hidden network
      int32_t rssi = WiFi.RSSI(i);
      bool secure = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
      bool merged = false;
      for (size_t j = 0; j < nets.size(); j++) {
        if (nets[j].ssid == ssid) {
          if (rssi > nets[j].rssi) { nets[j].rssi = rssi; nets[j].secure = secure; }
          merged = true;
          break;
        }
      }
      if (!merged) nets.push_back({ ssid, rssi, secure });
    }
    WiFi.scanDelete();
    std::sort(nets.begin(), nets.end(),
              [](const Net& a, const Net& b) { return a.rssi > b.rssi; });

    String json = "[";
    for (size_t i = 0; i < nets.size(); i++) {
      if (i > 0) json += ",";
      json += "{\"ssid\":\"" + jsonEscape(nets[i].ssid) + "\",\"rssi\":";
      json += String(nets[i].rssi);
      json += ",\"secure\":";
      json += nets[i].secure ? "true" : "false";
      json += "}";
    }
    json += "]";
    _webServer.send(200, "application/json", json);
  });
  _webServer.on("/save", HTTP_POST, [this]() {
    String ssid = _webServer.arg("ssid");
    String pass = _webServer.arg("pass");
    if (ssid.length() == 0) {
      _webServer.send(200, "text/html", buildConfigPage("SSID cannot be empty", _ssid));
      return;
    }
    saveCredentials(ssid, pass);
    _webServer.send(200, "text/html", buildSuccessPage(ssid));
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
