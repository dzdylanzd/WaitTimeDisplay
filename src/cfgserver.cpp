#include "cfgserver.h"
#include "config.h"
#include <ArduinoJson.h>

#if __has_include(<esp_task_wdt.h>)
#include <esp_task_wdt.h>
// The Arduino loopTask is not subscribed to the task watchdog on the C6,
// so a blind esp_task_wdt_reset() logs "task_wdt: task not found" on every
// call. Only feed it when the current task is actually subscribed.
static inline void feedWdtIfSubscribed() {
  if (esp_task_wdt_status(NULL) == ESP_OK) esp_task_wdt_reset();
}
#define FEED_WDT() feedWdtIfSubscribed()
#else
#define FEED_WDT() ((void)0)
#endif

static String minutesToHHMM(int minutes) {
  char buf[6];
  snprintf(buf, sizeof(buf), "%02d:%02d", (minutes / 60) % 24, minutes % 60);
  return String(buf);
}

static int hhmmToMinutes(const char* hhmm, int fallback) {
  if (!hhmm) return fallback;
  int h = 0, m = 0;
  if (sscanf(hhmm, "%d:%d", &h, &m) != 2) return fallback;
  if (h < 0 || h > 23 || m < 0 || m > 59) return fallback;
  return h * 60 + m;
}

// "#rrggbb" (or "rrggbb") → 0xRRGGBB; malformed input keeps the fallback.
static uint32_t parseHexColor(const char* s, uint32_t fallback) {
  if (!s) return fallback;
  if (*s == '#') s++;
  if (strlen(s) != 6) return fallback;
  char* end = nullptr;
  unsigned long v = strtoul(s, &end, 16);
  return (end && *end == '\0') ? (uint32_t)v : fallback;
}

static String hexColor(uint32_t c) {
  char buf[8];
  snprintf(buf, sizeof(buf), "#%06x", (unsigned)(c & 0xFFFFFF));
  return String(buf);
}

static String jsonEscape(const String& s) {
  String out;
  out.reserve(s.length() + 4);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s.charAt(i);
    switch (c) {
      case '\\': out += "\\\\"; break;
      case '"':  out += "\\\""; break;
      case '\n': out += "\\n";  break;
      case '\r': out += "\\r";  break;
      case '\t': out += "\\t";  break;
      default:   out += c;      break;
    }
  }
  return out;
}

static const char PROGMEM CONFIG_HTML[] = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>QueueWatch Config</title>
<style>
:root{
 --bg:#0b0a1a;--card:#141230;--card2:#1a1740;--input:#1c1940;
 --border:#2b2660;--border2:#3a3480;
 --accent:#7c5cff;--accent2:#a58cff;--gold:#ffd36b;
 --text:#ecebfa;--muted:#948dc2;
 --green:#00e676;--amber:#ffd600;--orange:#ff7a45;--red:#ff2d5e;--teal:#25e6e6;
}
*{box-sizing:border-box;margin:0;padding:0;font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif}
html{scroll-behavior:smooth}
.section{scroll-margin-top:64px}
.jumpnav{position:sticky;top:0;z-index:40;margin:-20px -16px 14px;padding:10px 16px;background:rgba(11,10,26,.92);backdrop-filter:blur(8px);border-bottom:1px solid var(--border)}
.jumpin{max-width:760px;margin:0 auto;display:flex;gap:8px;overflow-x:auto;scrollbar-width:none}
.jumpin::-webkit-scrollbar{display:none}
.jumpin a{flex:none;color:var(--text);text-decoration:none;font-size:.8rem;font-weight:600;padding:7px 13px;border-radius:20px;background:var(--input);border:1px solid var(--border2);transition:border-color .15s,background .15s}
.jumpin a:hover{border-color:var(--accent);background:var(--card2)}
body{background:var(--bg);background-image:radial-gradient(1100px 520px at 50% -8%,#2a1e5e 0%,rgba(11,10,26,0) 60%);color:var(--text);min-height:100vh;padding:20px 16px 120px;-webkit-font-smoothing:antialiased}
.container{max-width:760px;margin:0 auto}

.hero{background:linear-gradient(135deg,#3a2b86 0%,#221153 60%,#1a0f45 100%);border:1px solid var(--border2);border-radius:18px;padding:22px 24px;margin-bottom:20px;box-shadow:0 12px 40px rgba(0,0,0,.45)}
.hero h1{font-size:1.7rem;font-weight:800;letter-spacing:.2px;background:linear-gradient(90deg,#ffe39a,#ffd36b);-webkit-background-clip:text;background-clip:text;color:transparent}
.hero p{color:#c9c2f0;font-size:.9rem;margin-top:6px}
.hero .status{display:inline-flex;align-items:center;gap:7px;margin-top:12px;font-size:.8rem;color:#bdb4ee;background:rgba(255,255,255,.06);border:1px solid rgba(255,255,255,.09);padding:5px 12px;border-radius:20px}
.dot{width:8px;height:8px;border-radius:50%;background:var(--green);box-shadow:0 0 8px var(--green)}

.section{background:var(--card);border:1px solid var(--border);border-radius:16px;padding:20px;margin-bottom:18px;box-shadow:0 6px 22px rgba(0,0,0,.28)}
.section h2{display:flex;align-items:center;gap:9px;color:var(--text);font-size:1.15rem;font-weight:700;margin-bottom:4px}
.section h2 .ico{font-size:1.2rem}
.section .hint{color:var(--muted);font-size:.83rem;margin-bottom:16px}
.badge-count{margin-left:auto;font-size:.75rem;font-weight:600;color:var(--accent2);background:rgba(124,92,255,.14);border:1px solid rgba(124,92,255,.3);padding:3px 10px;border-radius:20px}

.grid{display:grid;grid-template-columns:1fr 1fr;gap:12px}
@media(max-width:520px){.grid{grid-template-columns:1fr}}
.field{background:var(--input);border:1px solid var(--border);border-radius:12px;padding:12px 14px;transition:border-color .15s}
.field:focus-within{border-color:var(--accent)}
.field label{display:block;color:var(--muted);font-size:.78rem;margin-bottom:8px}
.field .in{display:flex;align-items:baseline;gap:8px}
.field input{width:100%;background:transparent;border:none;color:#fff;font-size:1.3rem;font-weight:700;outline:none;min-width:0}
.field .unit{color:var(--muted);font-size:.8rem;font-weight:600}

.btn{display:inline-flex;align-items:center;gap:7px;padding:10px 18px;background:var(--accent);color:#fff;border:none;border-radius:10px;font-size:.9rem;font-weight:600;cursor:pointer;transition:transform .12s,background .2s}
.btn:hover{background:var(--accent2);transform:translateY(-1px)}
.btn:active{transform:translateY(0)}
.btn:disabled{opacity:.5;cursor:not-allowed;transform:none}
.btn-ghost{background:var(--input);border:1px solid var(--border2);color:var(--text)}
.btn-ghost:hover{background:var(--card2);border-color:var(--accent)}
.toolbar{display:flex;flex-wrap:wrap;gap:8px;margin-bottom:14px}

.search{position:relative;margin-bottom:12px}
.search input{width:100%;padding:11px 14px 11px 40px;background:var(--input);border:1px solid var(--border);border-radius:10px;color:#fff;font-size:.9rem;outline:none}
.search input:focus{border-color:var(--accent)}
.search .mag{position:absolute;left:13px;top:50%;transform:translateY(-50%);color:var(--muted);font-size:.9rem}

.grouphdr{color:var(--gold);font-size:.72rem;font-weight:700;text-transform:uppercase;letter-spacing:.6px;margin:14px 0 6px}
.item{display:flex;align-items:center;gap:12px;padding:9px 6px;border-radius:10px;transition:background .15s}
.item:hover{background:var(--card2)}
.item .name{flex:1;font-size:.92rem;color:var(--text);min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.item .rid{color:var(--muted);font-weight:600;font-size:.8rem;margin-right:2px}
.empty{color:var(--muted);font-size:.85rem;padding:10px 4px}

.switch{position:relative;display:inline-block;width:42px;height:24px;flex:none}
.switch input{opacity:0;width:0;height:0}
.slider{position:absolute;inset:0;background:var(--border2);border-radius:24px;transition:.2s;cursor:pointer}
.slider:before{content:"";position:absolute;height:18px;width:18px;left:3px;top:3px;background:#8a82b8;border-radius:50%;transition:.2s}
.switch input:checked+.slider{background:var(--accent)}
.switch input:checked+.slider:before{transform:translateX(18px);background:#fff}

.badge{display:inline-flex;align-items:baseline;gap:3px;font-weight:700;font-size:.95rem;padding:4px 9px;border-radius:8px;min-width:46px;justify-content:center;flex:none}
.badge small{font-size:.6rem;font-weight:600;opacity:.75}
.b-green{color:var(--green);background:rgba(0,230,118,.13);border:1px solid rgba(0,230,118,.32)}
.b-amber{color:var(--amber);background:rgba(255,214,0,.12);border:1px solid rgba(255,214,0,.3)}
.b-orange{color:var(--orange);background:rgba(255,122,69,.13);border:1px solid rgba(255,122,69,.32)}
.b-red{color:var(--red);background:rgba(255,45,94,.13);border:1px solid rgba(255,45,94,.32)}
.b-teal{color:var(--teal);background:rgba(37,230,230,.11);border:1px solid rgba(37,230,230,.3);font-size:.68rem;letter-spacing:.4px}
.b-muted{color:var(--muted);background:rgba(148,141,194,.1);border:1px solid var(--border2);font-size:.7rem}

.stats{display:flex;gap:10px;flex-wrap:wrap;margin:2px 0 14px}
.stat{flex:1;min-width:76px;background:var(--input);border:1px solid var(--border);border-radius:12px;padding:10px 12px;text-align:center}
.stat .v{font-size:1.3rem;font-weight:800}
.stat .k{font-size:.68rem;color:var(--muted);margin-top:2px;text-transform:uppercase;letter-spacing:.4px}

#parkSelector{width:100%;padding:11px 14px;background:var(--input);border:1px solid var(--border);border-radius:10px;color:#fff;font-size:.9rem;margin-bottom:14px;outline:none}
#parkSelector:focus{border-color:var(--accent)}

.savebar{position:fixed;left:0;right:0;bottom:0;background:rgba(11,10,26,.92);backdrop-filter:blur(10px);border-top:1px solid var(--border2);padding:14px 16px;z-index:50}
.savebar .inner{max-width:760px;margin:0 auto;display:flex;align-items:center;gap:14px}
.savebar .save{flex:1;justify-content:center;padding:13px;font-size:1rem}
.savebar .note{color:var(--muted);font-size:.78rem;display:none}
@media(min-width:560px){.savebar .note{display:block;flex:1}.savebar .save{flex:none;min-width:240px}}

.spinner{display:inline-block;width:15px;height:15px;border:2px solid rgba(255,255,255,.25);border-top-color:#fff;border-radius:50%;animation:spin .8s linear infinite;vertical-align:-2px}
@keyframes spin{to{transform:rotate(360deg)}}
.toast{position:fixed;left:50%;bottom:90px;transform:translateX(-50%) translateY(20px);opacity:0;pointer-events:none;transition:.25s;padding:12px 20px;border-radius:12px;font-size:.9rem;font-weight:600;box-shadow:0 10px 30px rgba(0,0,0,.4);z-index:60;max-width:90vw;text-align:center}
.toast.show{opacity:1;transform:translateX(-50%) translateY(0)}
.toast.success{background:#0e3d2c;color:#5df0b8;border:1px solid #1c7a57}
.toast.error{background:#3d0f1a;color:#ff8fa6;border:1px solid #7a2340}
.toast.info{background:#152a52;color:#8fc0ff;border:1px solid #2a4a8a}

.star{background:none;border:none;font-size:1.15rem;color:var(--border2);cursor:pointer;flex:none;padding:0 4px;line-height:1;transition:color .15s,transform .12s}
.star:hover{transform:scale(1.15)}
.star.on{color:var(--gold)}
.fieldsel{width:100%;background:transparent;border:none;color:#fff;font-size:1rem;font-weight:700;outline:none}
.fieldsel option{background:var(--card)}
.field input[type=range]{accent-color:var(--accent);font-size:1rem;padding:6px 0}
.field input[type=time]{color-scheme:dark;font-size:1.05rem}
.field input[type=color]{width:100%;height:36px;border:none;background:transparent;padding:0;cursor:pointer}
.palrow{display:flex;gap:10px;flex-wrap:wrap;margin-top:8px}
.palcard{flex:1;min-width:92px;max-width:136px;background:var(--input);border:2px solid var(--border);border-radius:12px;padding:8px;cursor:pointer;text-align:center;transition:border-color .15s,transform .12s}
.palcard:hover{transform:translateY(-1px);border-color:var(--border2)}
.palcard.sel{border-color:var(--accent)}
.palprev{border-radius:8px;overflow:hidden;height:44px;display:flex;flex-direction:column}
.palprev .ph{height:16px}.palprev .pa{height:4px}.palprev .pp{flex:1}
.palcard .pn{font-size:.72rem;font-weight:600;color:var(--muted);margin-top:6px}
.palcard.sel .pn{color:var(--text)}
</style>
</head>
)rawliteral"
R"rawliteral(
<body>
<div class="jumpnav"><div class="jumpin">
  <a href="#sec-timing">&#9201; Timing</a>
  <a href="#sec-parks">&#127983; Parks</a>
  <a href="#sec-rides">&#127903; Rides</a>
  <a href="#sec-display">&#128161; Display</a>
  <a href="#sec-wait">&#127912; Wait colours</a>
  <a href="#sec-options">&#11088; Options</a>
  <a href="#sec-backup">&#128190; Backup</a>
  <a href="#sec-danger">&#9888; Reset</a>
</div></div>
<div class="container">
  <div class="hero">
    <h1>&#127906; QueueWatch</h1>
    <p>Live theme-park wait times on your desk. Configure parks, rides &amp; timing below.</p>
    <div class="status"><span class="dot"></span><span id="statusText">Device connected</span></div>
  </div>
  <form id="configForm" onsubmit="return saveConfig(event)">

  <div class="section" id="sec-timing">
    <h2><span class="ico">&#9201;</span> Timing</h2>
    <p class="hint">How often the device refreshes live data and rotates through rides.</p>
    <div class="grid">
      <div class="field"><label>API refresh</label><div class="in"><input type="number" id="api_int" min="30" max="3600" step="1" required><span class="unit">sec</span></div></div>
      <div class="field"><label>Ride rotate</label><div class="in"><input type="number" id="rot_int" min="3" max="300" step="1" required><span class="unit">sec</span></div></div>
      <div class="field"><label>Closed-park display</label><div class="in"><input type="number" id="closed_int" min="5" max="300" step="1" required><span class="unit">sec</span></div></div>
      <div class="field"><label>Clock update</label><div class="in"><input type="number" id="time_int" min="10" max="600" step="1" required><span class="unit">sec</span></div></div>
    </div>
  </div>

  <div class="section" id="sec-parks">
    <h2><span class="ico">&#127983;</span> Parks <span class="badge-count" id="parkCount">0 selected</span></h2>
    <p class="hint">Choose which parks to cycle through on the display.</p>
    <div class="toolbar">
      <button type="button" class="btn btn-ghost" id="refreshBtn" onclick="fetchParks(this)">&#8635; Refresh</button>
      <button type="button" class="btn btn-ghost" onclick="selectAllParks(true)">Select all</button>
      <button type="button" class="btn btn-ghost" onclick="selectAllParks(false)">Clear</button>
    </div>
    <div class="search"><span class="mag">&#128269;</span><input type="text" id="parkSearch" placeholder="Search parks..." oninput="renderParkList()"></div>
    <div id="parkList"></div>
  </div>

  <div class="section" id="sec-rides">
    <h2><span class="ico">&#127903;</span> Rides &amp; Wait Times</h2>
    <p class="hint">Pick a park to see current wait times, then toggle individual rides on or off.</p>
    <select id="parkSelector"><option value="">-- Select a park --</option></select>
    <div id="rideStats"></div>
    <div class="toolbar" id="rideTools" style="display:none">
      <button type="button" class="btn btn-ghost" onclick="selectAllRides(true)">All on</button>
      <button type="button" class="btn btn-ghost" onclick="selectAllRides(false)">All off</button>
    </div>
    <div id="rideList"></div>
  </div>
  </form>
</div>

<div class="savebar"><div class="inner">
  <span class="note" id="saveNote">Saving restarts the on-device display cycle with your new settings.</span>
  <button type="submit" form="configForm" class="btn save" id="saveBtn">&#10003; Save &amp; Restart Cycle</button>
</div></div>
<div id="toast" class="toast"></div>
)rawliteral"
R"rawliteral(
<div class="container">
  <div class="section" id="sec-display">
    <h2><span class="ico">&#128161;</span> Display</h2>
    <p class="hint">Screen &amp; LED brightness, plus optional quiet hours when the display dims or switches off. Pick your own timezone below so quiet hours follow the clock on your wall &mdash; not the park's.</p>
    <div class="grid">
      <div class="field"><label>Brightness &mdash; <span id="brtVal">100</span>%</label>
        <input type="range" id="brt" min="5" max="100" value="100" oninput="$('brtVal').textContent=this.value"></div>
      <div class="field"><label>Status LED (wait colour)</label>
        <label class="switch"><input type="checkbox" id="led_en" checked><span class="slider"></span></label></div>
      <div class="field"><label>Flip screen 180&deg;</label>
        <label class="switch"><input type="checkbox" id="flip_scr"><span class="slider"></span></label></div>
      <div class="field"><label>Quiet hours</label>
        <label class="switch"><input type="checkbox" id="qt_en"><span class="slider"></span></label></div>
      <div class="field"><label>Quiet start</label><input type="time" id="qt_sta" value="22:00"></div>
      <div class="field"><label>Quiet end</label><input type="time" id="qt_end" value="07:00"></div>
      <div class="field"><label>Quiet brightness &mdash; <span id="qtBrtVal">0</span>% (0 = off)</label>
        <input type="range" id="qt_brt" min="0" max="100" value="0" oninput="$('qtBrtVal').textContent=this.value"></div>
      <div class="field"><label>Quiet-hours timezone</label>
        <select id="dev_tz" class="fieldsel"><option value="">Same as displayed park</option></select></div>
    </div>
    <div class="grouphdr" style="margin-top:16px">Colour palette</div>
    <div class="palrow" id="palRow"></div>
  </div>
  <div class="section" id="sec-wait">
    <h2><span class="ico">&#127912;</span> Wait colours</h2>
    <p class="hint">When a wait counts as short, medium or long &mdash; and which colour each level shows on the screen and the LED. Waits above the last threshold use the &ldquo;very long&rdquo; colour.</p>
    <div class="grid">
      <div class="field"><label>Short wait up to</label><div class="in"><input type="number" id="wt1" min="1" max="238" step="1" value="15"><span class="unit">min</span></div></div>
      <div class="field"><label>Medium wait up to</label><div class="in"><input type="number" id="wt2" min="2" max="239" step="1" value="30"><span class="unit">min</span></div></div>
      <div class="field"><label>Long wait up to</label><div class="in"><input type="number" id="wt3" min="3" max="240" step="1" value="45"><span class="unit">min</span></div></div>
    </div>
    <div class="grid" style="margin-top:12px">
      <div class="field"><label>Short wait</label><input type="color" id="wc0" value="#00e676"></div>
      <div class="field"><label>Medium wait</label><input type="color" id="wc1" value="#ffd600"></div>
      <div class="field"><label>Long wait</label><input type="color" id="wc2" value="#ff7043"></div>
      <div class="field"><label>Very long wait</label><input type="color" id="wc3" value="#ff1744"></div>
      <div class="field"><label>Ride closed</label><input type="color" id="wc4" value="#18ffff"></div>
    </div>
    <div class="toolbar" style="margin:14px 0 0">
      <button type="button" class="btn btn-ghost" onclick="resetWaitDefaults()">Reset to defaults</button>
    </div>
  </div>
  <div class="section" id="sec-options">
    <h2><span class="ico">&#11088;</span> Ride display options</h2>
    <p class="hint">How rides are ordered and filtered on the device. Mark favorites with the star in the ride list above &mdash; they get a gold marker on the display.</p>
    <div class="grid">
      <div class="field"><label>Sort order</label>
        <select id="sortMode" class="fieldsel"><option value="0">Park order</option><option value="1">Longest wait first</option></select></div>
      <div class="field"><label>Favorites first</label>
        <label class="switch"><input type="checkbox" id="favFirst" checked><span class="slider"></span></label></div>
      <div class="field"><label>Skip closed rides</label>
        <label class="switch"><input type="checkbox" id="skipClosed"><span class="slider"></span></label></div>
      <div class="field"><label>Hide waits under</label>
        <div class="in"><input type="number" id="minWait" min="0" max="240" step="1" value="0"><span class="unit">min</span></div></div>
    </div>
  </div>
</div>
)rawliteral"
R"rawliteral(
<div class="container">
  <div class="section" id="sec-backup">
    <h2><span class="ico">&#128190;</span> Backup &amp; Restore</h2>
    <p class="hint">Export the current parks, ride filters and timing settings to a file, or restore a previously exported configuration.</p>
    <div class="toolbar" style="margin-bottom:0">
      <button type="button" class="btn btn-ghost" onclick="exportConfig(this)">&#8681; Export config</button>
      <button type="button" class="btn btn-ghost" onclick="$('importFile').click()">&#8679; Import config</button>
      <input type="file" id="importFile" accept=".json,application/json" style="display:none" onchange="importConfig(this)">
    </div>
  </div>
  <div class="section" style="border-color:#5a2440" id="sec-danger">
    <h2><span class="ico">&#9888;&#65039;</span> Danger zone</h2>
    <p class="hint">Factory reset erases the WiFi credentials, parks, ride filters and timing settings, then restarts the device into WiFi setup mode.</p>
    <button type="button" class="btn" style="background:#c2183f" onclick="factoryReset(this)">Factory Reset</button>
  </div>
</div>
<script>
async function factoryReset(btn){
  if(!confirm('Factory reset will erase the WiFi credentials and ALL settings, then restart the device.\n\nContinue?'))return;
  btn.disabled=true;btn.innerHTML='<span class="spinner"></span> Resetting...';
  try{const res=await fetch('/api/factory-reset',{method:'POST'});
    const r=await res.json();
    if(r.success){toast('Reset done. The device is restarting into WiFi setup.','info');btn.innerHTML='Device restarting...';return;}
    toast('Error: '+(r.error||'unknown'),'error');
  }catch(e){toast('Reset request failed','error');}
  btn.disabled=false;btn.innerHTML='Factory Reset';}
</script>
<script>
let allParks=[];let allRides=[];let currentParkId=0;let rideFilterCache={};let favCache={};
const $=id=>document.getElementById(id);

// Zones the firmware can map to POSIX rules — keep in sync with the
// TZ_TABLE in src/tzhelper.cpp.
const TZ_LIST=['America/Chicago','America/Denver','America/Detroit','America/Halifax','America/Los_Angeles','America/Mexico_City','America/New_York','America/Phoenix','America/Sao_Paulo','America/Toronto','America/Vancouver','Asia/Bangkok','Asia/Beijing','Asia/Dubai','Asia/Hong_Kong','Asia/Istanbul','Asia/Jakarta','Asia/Kolkata','Asia/Kuala_Lumpur','Asia/Macau','Asia/Muscat','Asia/Riyadh','Asia/Seoul','Asia/Shanghai','Asia/Singapore','Asia/Taipei','Asia/Tokyo','Australia/Brisbane','Australia/Melbourne','Australia/Perth','Australia/Sydney','Europe/Amsterdam','Europe/Berlin','Europe/Brussels','Europe/Budapest','Europe/Copenhagen','Europe/Dublin','Europe/Helsinki','Europe/Lisbon','Europe/London','Europe/Madrid','Europe/Oslo','Europe/Paris','Europe/Prague','Europe/Rome','Europe/Stockholm','Europe/Vienna','Europe/Warsaw','Europe/Zurich','Pacific/Auckland','Pacific/Guam','Pacific/Honolulu'];

// Palette swatches — order and hexes must match PALETTES[] in src/display.cpp
// (h=header, a=accent, p=ride panel; count: COLOR_PALETTE_COUNT).
const PALETTE_DEFS=[
 {n:'Magic Night',h:'#2A0860',a:'#FFD466',p:'#160A34'},
 {n:'Deep Ocean',h:'#04386E',a:'#7DF3E8',p:'#06182E'},
 {n:'Sunset Ember',h:'#6E1A08',a:'#FF8C1A',p:'#2A0E06'},
 {n:'Forest Twilight',h:'#0C4A20',a:'#9AE22E',p:'#0A2012'},
 {n:'Carbon Mono',h:'#3A3A40',a:'#E0E0E4',p:'#1A1A1E'}];
let selectedPal=0;
function renderPalRow(){let html='';
  PALETTE_DEFS.forEach((d,i)=>{
    html+='<div class="palcard'+(i===selectedPal?' sel':'')+'" onclick="selectedPal='+i+';renderPalRow()">'
      +'<div class="palprev"><div class="ph" style="background:'+d.h+'"></div>'
      +'<div class="pa" style="background:'+d.a+'"></div>'
      +'<div class="pp" style="background:'+d.p+'"></div></div>'
      +'<div class="pn">'+d.n+'</div></div>';});
  $('palRow').innerHTML=html;}

const WAIT_DEFAULTS={th:[15,30,45],cols:['#00e676','#ffd600','#ff7043','#ff1744','#18ffff']};
function resetWaitDefaults(){
  ['wt1','wt2','wt3'].forEach((id,i)=>$(id).value=WAIT_DEFAULTS.th[i]);
  WAIT_DEFAULTS.cols.forEach((c,i)=>$('wc'+i).value=c);}
function readWaitThresholds(){
  let t=[parseInt($('wt1').value)||15,parseInt($('wt2').value)||30,parseInt($('wt3').value)||45];
  if(t[1]<=t[0])t[1]=t[0]+1; if(t[2]<=t[1])t[2]=t[1]+1; return t;}
function readWaitColors(){return [0,1,2,3,4].map(i=>$('wc'+i).value);}

function populateTzSelector(){const sel=$('dev_tz');
  let browserTz='';try{browserTz=Intl.DateTimeFormat().resolvedOptions().timeZone;}catch(e){}
  for(const tz of TZ_LIST){const o=document.createElement('option');o.value=tz;
    o.textContent=tz.replace('_',' ')+(tz===browserTz?' (your timezone)':'');sel.appendChild(o);}}

window.addEventListener('DOMContentLoaded',async()=>{
  populateTzSelector();renderPalRow();
  try{const res=await fetch('/api/config');const cfg=await res.json();
    $('api_int').value=cfg.apiRefreshInterval;
    $('rot_int').value=cfg.rotateInterval;
    $('closed_int').value=cfg.closedParkDisplayTime;
    $('time_int').value=cfg.timeUpdateInterval;
    if(typeof cfg.brightness==='number'){$('brt').value=cfg.brightness;$('brtVal').textContent=cfg.brightness;}
    $('qt_en').checked=!!cfg.quietEnabled;
    if(cfg.quietStart)$('qt_sta').value=cfg.quietStart;
    if(cfg.quietEnd)$('qt_end').value=cfg.quietEnd;
    if(typeof cfg.quietBrightness==='number'){$('qt_brt').value=cfg.quietBrightness;$('qtBrtVal').textContent=cfg.quietBrightness;}
    if(typeof cfg.ledEnabled==='boolean')$('led_en').checked=cfg.ledEnabled;
    if(typeof cfg.flipScreen==='boolean')$('flip_scr').checked=cfg.flipScreen;
    if(typeof cfg.deviceTimezone==='string')$('dev_tz').value=cfg.deviceTimezone;
    if(typeof cfg.colorPalette==='number'){selectedPal=cfg.colorPalette;renderPalRow();}
    if(Array.isArray(cfg.waitThresholds)&&cfg.waitThresholds.length===3){
      $('wt1').value=cfg.waitThresholds[0];$('wt2').value=cfg.waitThresholds[1];$('wt3').value=cfg.waitThresholds[2];}
    if(Array.isArray(cfg.waitColors)&&cfg.waitColors.length===5)
      cfg.waitColors.forEach((c,i)=>$('wc'+i).value=c);
    if(cfg.rideOptions){$('sortMode').value=cfg.rideOptions.sortMode||0;$('favFirst').checked=!!cfg.rideOptions.favoritesFirst;$('skipClosed').checked=!!cfg.rideOptions.skipClosed;$('minWait').value=cfg.rideOptions.minWait||0;}
    if(cfg.rideFavorites)favCache=cfg.rideFavorites;
    if(cfg.enabledParks&&cfg.enabledParks.length){
      allParks=cfg.enabledParks.map(p=>({...p,enabled:true,group:"Configured"}));
      renderParkList();populateParkSelector(cfg.enabledParks);
      currentParkId=cfg.currentParkId||0;
      if(currentParkId)fetchRidesForPark(currentParkId);
    }
  }catch(e){toast('Failed to load config','error');}
  // Auto-load the full park list so the user does not have to click Refresh.
  fetchParks($('refreshBtn'));
});

async function fetchParks(btn){if(!btn)return;btn.disabled=true;btn.innerHTML='<span class="spinner"></span> Loading...';
  try{const res=await fetch('/api/parks');
    if(!res.ok){const e=await res.json().catch(()=>({}));throw new Error(e.error||'HTTP '+res.status);}
    allParks=await res.json();renderParkList();
    populateParkSelector(allParks.filter(p=>p.enabled));
    toast(allParks.length+' parks loaded','success');
  }catch(e){toast('Failed to fetch parks: '+e.message,'error');}
  btn.disabled=false;btn.innerHTML='&#8635; Refresh';}

function updateParkCount(){const n=allParks.filter(p=>p.enabled).length;$('parkCount').textContent=n+' selected';}

function renderParkList(){const c=$('parkList');updateParkCount();
  if(!allParks.length){c.innerHTML='<p class="empty">Tap &ldquo;Refresh&rdquo; to load the park list.</p>';return;}
  const q=($('parkSearch').value||'').toLowerCase();
  let html='',lastGroup='',shown=0;
  for(const park of allParks){
    if(q&&park.name.toLowerCase().indexOf(q)<0)continue;
    shown++;
    if(park.group!==lastGroup){lastGroup=park.group;html+='<div class="grouphdr">'+escapeHtml(park.group)+'</div>';}
    html+='<div class="item"><label class="switch"><input type="checkbox" id="pk_'+park.id+'" '+(park.enabled?'checked':'')+' onchange="togglePark('+park.id+')"><span class="slider"></span></label><span class="name">'+escapeHtml(park.name)+'</span></div>';
  }
  if(!shown)html='<p class="empty">No parks match your search.</p>';
  c.innerHTML=html;}

function togglePark(id){const park=allParks.find(p=>p.id===id);if(park){park.enabled=!park.enabled;updateParkCount();populateParkSelector(allParks.filter(p=>p.enabled));}}
function selectAllParks(select){for(const park of allParks)park.enabled=select;renderParkList();populateParkSelector(select?allParks:[]);}

function populateParkSelector(parks){const sel=$('parkSelector');const cur=currentParkId;
  let html='<option value="">-- Select a park --</option>';
  if(parks&&parks.length){for(const park of parks)html+='<option value="'+park.id+'"'+(park.id===cur?' selected':'')+'>'+escapeHtml(park.name)+'</option>';}
  sel.innerHTML=html;
  sel.onchange=function(){saveCurrentRideFilterState();const id=parseInt(this.value);if(id)fetchRidesForPark(id);else clearRides();};}

function clearRides(){currentParkId=0;allRides=[];$('rideList').innerHTML='';$('rideStats').innerHTML='';$('rideTools').style.display='none';}

function saveCurrentRideFilterState(){if(!currentParkId)return;
  const enabledIds=allRides.filter(r=>r.enabled).map(r=>r.id);
  rideFilterCache[currentParkId]=(enabledIds.length===allRides.length)?null:enabledIds;
  const favIds=allRides.filter(r=>r.favorite).map(r=>r.id);
  favCache[currentParkId]=favIds.length?favIds:null;}

async function fetchRidesForPark(parkId){currentParkId=parkId;
  const c=$('rideList');c.innerHTML='<p class="empty"><span class="spinner"></span> Loading rides...</p>';
  $('rideStats').innerHTML='';$('rideTools').style.display='none';
  try{const res=await fetch('/api/rides?parkId='+parkId);allRides=await res.json();
    if(rideFilterCache.hasOwnProperty(parkId)){const cached=rideFilterCache[parkId];
      for(const ride of allRides)ride.enabled=cached===null?true:cached.includes(ride.id);}
    if(favCache.hasOwnProperty(parkId)){const favs=favCache[parkId];
      for(const ride of allRides)ride.favorite=favs===null?false:favs.includes(ride.id);}
    renderRideList();
  }catch(e){c.innerHTML='<p class="empty" style="color:var(--red)">Failed to load rides.</p>';}}

)rawliteral"
R"rawliteral(
// Badge colours follow the user-configured thresholds + level colours so the
// browser list matches what the device (and its LED) will show.
function levelStyle(c){return 'style="color:'+c+';background:'+c+'22;border:1px solid '+c+'55"';}
function waitBadge(r){
  if(r.isOpen===false)return '<span class="badge" '+levelStyle($('wc4').value)+'>CLOSED</span>';
  if(r.waitTime<0)return '<span class="badge b-muted">n/a</span>';
  const t=readWaitThresholds();
  const i=r.waitTime<=t[0]?0:r.waitTime<=t[1]?1:r.waitTime<=t[2]?2:3;
  return '<span class="badge" '+levelStyle($('wc'+i).value)+'>'+r.waitTime+'<small>min</small></span>';}

function renderRideStats(){
  const openRides=allRides.filter(r=>r.isOpen);
  const timed=openRides.filter(r=>r.waitTime>=0);
  let avg=0,max=0;
  for(const r of timed){avg+=r.waitTime;if(r.waitTime>max)max=r.waitTime;}
  if(timed.length)avg=Math.round(avg/timed.length);
  $('rideStats').innerHTML='<div class="stats">'
    +'<div class="stat"><div class="v">'+allRides.length+'</div><div class="k">Rides</div></div>'
    +'<div class="stat"><div class="v" style="color:var(--green)">'+openRides.length+'</div><div class="k">Open</div></div>'
    +'<div class="stat"><div class="v" style="color:var(--gold)">'+(timed.length?avg:'--')+'</div><div class="k">Avg min</div></div>'
    +'<div class="stat"><div class="v" style="color:var(--orange)">'+(timed.length?max:'--')+'</div><div class="k">Max min</div></div>'
    +'</div>';}

function renderRideList(){const c=$('rideList');
  if(!allRides.length){c.innerHTML='<p class="empty">No rides available for this park.</p>';$('rideStats').innerHTML='';$('rideTools').style.display='none';return;}
  $('rideTools').style.display='flex';renderRideStats();
  let html='';
  for(const ride of allRides){
    html+='<div class="item"><label class="switch"><input type="checkbox" id="ride_'+ride.id+'" '+(ride.enabled?'checked':'')+' onchange="toggleRide('+ride.id+')"><span class="slider"></span></label><span class="name"><span class="rid">#'+ride.id+'</span> '+escapeHtml(ride.name)+'</span><button type="button" class="star'+(ride.favorite?' on':'')+'" title="Favorite" onclick="toggleFav('+ride.id+',this)">&#9733;</button>'+waitBadge(ride)+'</div>';
  }
  c.innerHTML=html;}

function toggleRide(id){const ride=allRides.find(r=>r.id===id);if(ride)ride.enabled=!ride.enabled;}
function toggleFav(id,el){const ride=allRides.find(r=>r.id===id);if(!ride)return;
  ride.favorite=!ride.favorite;el.classList.toggle('on',ride.favorite);}
function selectAllRides(select){for(const ride of allRides)ride.enabled=select;renderRideList();}

async function saveConfig(event){event.preventDefault();saveCurrentRideFilterState();
  const btn=$('saveBtn');btn.disabled=true;btn.innerHTML='<span class="spinner"></span> Saving...';
  const enabledParks=allParks.filter(p=>p.enabled).map(p=>({id:p.id,name:p.name}));
  const rideFilters={};for(const park of enabledParks){const cached=rideFilterCache[park.id];if(cached===undefined)continue;rideFilters[park.id]=(cached&&cached.length>0)?cached:null;}
  const rideFavorites={};for(const park of enabledParks){const f=favCache[park.id];if(f===undefined)continue;rideFavorites[park.id]=(f&&f.length>0)?f:null;}
  const body={apiRefreshInterval:parseInt($('api_int').value),rotateInterval:parseInt($('rot_int').value),closedParkDisplayTime:parseInt($('closed_int').value),timeUpdateInterval:parseInt($('time_int').value),enabledParks,rideFilters,rideFavorites,
    brightness:parseInt($('brt').value),quietEnabled:$('qt_en').checked,quietStart:$('qt_sta').value||'22:00',quietEnd:$('qt_end').value||'07:00',quietBrightness:parseInt($('qt_brt').value),ledEnabled:$('led_en').checked,flipScreen:$('flip_scr').checked,deviceTimezone:$('dev_tz').value,colorPalette:selectedPal,waitThresholds:readWaitThresholds(),waitColors:readWaitColors(),
    rideOptions:{sortMode:parseInt($('sortMode').value),favoritesFirst:$('favFirst').checked,skipClosed:$('skipClosed').checked,minWait:parseInt($('minWait').value)||0}};
  try{const res=await fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)});
    const result=await res.json();
    if(result.success)toast('&#10003; Saved! Display cycle restarting...','success');
    else toast('Error: '+(result.error||'unknown'),'error');
  }catch(e){toast('Failed to save configuration','error');}
  btn.disabled=false;btn.innerHTML='&#10003; Save &amp; Restart Cycle';return false;}

async function exportConfig(btn){btn.disabled=true;
  try{const res=await fetch('/api/config');if(!res.ok)throw new Error('HTTP '+res.status);
    const cfg=await res.json();
    const blob=new Blob([JSON.stringify(cfg,null,2)],{type:'application/json'});
    const a=document.createElement('a');a.href=URL.createObjectURL(blob);
    const d=new Date(),p=n=>String(n).padStart(2,'0');
    a.download='queuewatch-config-'+d.getFullYear()+p(d.getMonth()+1)+p(d.getDate())+'.json';
    a.click();URL.revokeObjectURL(a.href);
    toast('Config exported','success');
  }catch(e){toast('Export failed: '+e.message,'error');}
  btn.disabled=false;}

async function importConfig(input){const file=input.files[0];input.value='';if(!file)return;
  let cfg;
  try{cfg=JSON.parse(await file.text());}catch(e){toast('Not a valid JSON file','error');return;}
  if(!cfg||typeof cfg!=='object'||!Array.isArray(cfg.enabledParks)||typeof cfg.apiRefreshInterval!=='number'){
    toast('Not a QueueWatch config file','error');return;}
  if(!confirm('Import "'+file.name+'"?\n\nThis replaces the parks, ride filters and timing settings on the device.'))return;
  // Explicit null clears stale filters for parks kept from the old config.
  const rideFilters={};const src=cfg.rideFilters||{};
  for(const park of cfg.enabledParks)rideFilters[park.id]=src.hasOwnProperty(park.id)?src[park.id]:null;
  const rideFavorites={};const favSrc=cfg.rideFavorites||{};
  for(const park of cfg.enabledParks)rideFavorites[park.id]=favSrc.hasOwnProperty(park.id)?favSrc[park.id]:null;
  const body={apiRefreshInterval:cfg.apiRefreshInterval,rotateInterval:cfg.rotateInterval,
    closedParkDisplayTime:cfg.closedParkDisplayTime,timeUpdateInterval:cfg.timeUpdateInterval,
    enabledParks:cfg.enabledParks.map(p=>({id:p.id,name:p.name})),rideFilters,rideFavorites};
  // Newer fields ride along when present (older exports simply omit them).
  if(typeof cfg.brightness==='number')body.brightness=cfg.brightness;
  if(typeof cfg.quietEnabled==='boolean'){body.quietEnabled=cfg.quietEnabled;
    if(cfg.quietStart)body.quietStart=cfg.quietStart;
    if(cfg.quietEnd)body.quietEnd=cfg.quietEnd;
    if(typeof cfg.quietBrightness==='number')body.quietBrightness=cfg.quietBrightness;}
  if(typeof cfg.ledEnabled==='boolean')body.ledEnabled=cfg.ledEnabled;
  if(typeof cfg.flipScreen==='boolean')body.flipScreen=cfg.flipScreen;
  if(typeof cfg.deviceTimezone==='string')body.deviceTimezone=cfg.deviceTimezone;
  if(typeof cfg.colorPalette==='number')body.colorPalette=cfg.colorPalette;
  if(Array.isArray(cfg.waitThresholds))body.waitThresholds=cfg.waitThresholds;
  if(Array.isArray(cfg.waitColors))body.waitColors=cfg.waitColors;
  if(cfg.rideOptions)body.rideOptions=cfg.rideOptions;
  try{const res=await fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)});
    const r=await res.json();
    if(r.success){toast('&#10003; Config imported! Reloading...','success');setTimeout(()=>location.reload(),1200);}
    else toast('Import error: '+(r.error||'unknown'),'error');
  }catch(e){toast('Import request failed','error');}}

let toastTimer;
function toast(text,type){const t=$('toast');t.className='toast '+(type||'info');t.innerHTML=text;
  requestAnimationFrame(()=>t.classList.add('show'));
  clearTimeout(toastTimer);toastTimer=setTimeout(()=>t.classList.remove('show'),3200);}

function escapeHtml(str){return String(str).replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/"/g,'&quot;').replace(/'/g,'&#039;');}
</script>
</body>
</html>
)rawliteral";

// Merge an incoming per-park map ({"parkId":[rideIds]} with null meaning
// "clear this park's entry") into the stored JSON, dropping parks that are no
// longer enabled. Used for both ride filters and ride favorites. Returns
// false when the merged result would blow the NVS string budget — callers
// must then reject the save (truncating would store unparseable JSON).
static bool mergePerParkJson(const String& existing, JsonObject incoming,
                             const std::vector<int>& keepParkIds, String& out) {
  DynamicJsonDocument mergedDoc(8192);
  JsonObject merged;
  if (existing.length() > 0 && !deserializeJson(mergedDoc, existing)
      && mergedDoc.is<JsonObject>()) {
    merged = mergedDoc.as<JsonObject>();      // reuse existing (writable)
  } else {
    mergedDoc.clear();
    merged = mergedDoc.to<JsonObject>();      // fresh, writable object
  }

  if (!incoming.isNull()) {
    for (JsonPair kv : incoming) {
      if (kv.value().isNull()) merged.remove(kv.key());   // clear this park
      else                     merged[kv.key()] = kv.value();
    }
  }

  // Remove entries for parks that are no longer enabled
  std::vector<String> keysToRemove;
  for (JsonPair kv : merged) {
    int parkId = atoi(kv.key().c_str());
    bool stillEnabled = false;
    for (size_t i = 0; i < keepParkIds.size(); i++) {
      if (keepParkIds[i] == parkId) { stillEnabled = true; break; }
    }
    if (!stillEnabled) keysToRemove.push_back(String(kv.key().c_str()));
  }
  for (size_t i = 0; i < keysToRemove.size(); i++) merged.remove(keysToRemove[i].c_str());

  out = "";
  if (merged.size() > 0) {
    serializeJson(merged, out);
    if (out.length() > 1900) return false;    // NVS string budget
  }
  return true;
}

ConfigWebServer::ConfigWebServer(ConfigManager& cfgMgr, QueueApi& api)
  : _server(80), _cfgMgr(cfgMgr), _api(api), _configUpdated(false) {}

// Idempotent: safe to call when already running, and safe to call again after
// stop() — the WiFi portal stops this server and reconnect must revive it.
void ConfigWebServer::begin() {
  if (_started) return;
  if (!_handlersRegistered) {
    _server.on("/",             [this]() { handleRoot(); });
    _server.on("/api/parks",    [this]() { handleApiParks(); });
    _server.on("/api/rides",    [this]() { handleApiRides(); });
    _server.on("/api/config", HTTP_GET,  [this]() { handleApiConfig(); });
    _server.on("/api/config", HTTP_POST, [this]() { handleSaveConfig(); });
    _server.on("/api/factory-reset", HTTP_POST, [this]() { handleFactoryReset(); });
    _server.onNotFound([this]() { handleNotFound(); });
    _handlersRegistered = true;
  }
  _server.begin();
  _started = true;
  Serial.println("Config web server started on port 80");
}

void ConfigWebServer::handleClient() {
  _server.handleClient();
  // Restart is deferred to the main loop so the HTTP response reaches the
  // browser first (in the sim the server runs on a background thread).
  if (_pendingFactoryReset) {
    delay(400);
    ESP.restart();
  }
}
void ConfigWebServer::stop() {
  _server.close();
  _started = false;
}
bool ConfigWebServer::isConfigUpdated()    { return _configUpdated; }
void ConfigWebServer::clearConfigFlag()    { _configUpdated = false; }

void ConfigWebServer::handleRoot() {
  _server.send(200, "text/html", buildConfigPage());
}

void ConfigWebServer::handleApiParks() {
  std::vector<int> ids; std::vector<String> names; std::vector<String> groups;
  if (!_api.fetchAvailableParks(ids, names, groups)) {
    _server.send(500, "application/json", "{\"error\":\"Failed to fetch parks\"}");
    return;
  }
  const RuntimeConfig& cfg = _cfgMgr.getConfig();
  // Build JSON as a plain string to avoid ArduinoJson pool-size issues on 64-bit platforms.
  // (ArduinoJson needs ~2× the output size internally on 64-bit; doc(16384) silently
  //  drops fields when building responses with 100+ parks on Windows.)
  String json = "[";
  for (size_t i = 0; i < ids.size(); i++) {
    bool enabled = false;
    for (size_t j = 0; j < cfg.enabledParkIds.size(); j++) {
      if (cfg.enabledParkIds[j] == ids[i]) { enabled = true; break; }
    }
    if (i > 0) json += ",";
    json += "{\"id\":";  json += ids[i];
    json += ",\"name\":\""; json += jsonEscape(names[i]); json += "\"";
    json += ",\"group\":\""; json += jsonEscape(groups[i]); json += "\"";
    json += ",\"enabled\":"; json += enabled ? "true" : "false";
    json += "}";
  }
  json += "]";
  _server.send(200, "application/json", json);
}

void ConfigWebServer::handleApiRides() {
  if (!_server.hasArg("parkId")) {
    _server.send(400, "application/json", "{\"error\":\"Missing parkId\"}"); return;
  }
  int parkId = _server.arg("parkId").toInt();
  if (parkId <= 0) {
    _server.send(400, "application/json", "{\"error\":\"Invalid parkId\"}"); return;
  }
  RideInfo rides[MAX_RIDES]; int rideCount = 0;
  if (!_api.fetchRideData(parkId, rides, rideCount, MAX_RIDES)) {
    _server.send(500, "application/json", "{\"error\":\"Failed to fetch rides\"}"); return;
  }
  // Build JSON as a plain string — same reason as handleApiParks().
  String json = "[";
  for (int i = 0; i < rideCount; i++) {
    if (i > 0) json += ",";
    json += "{\"id\":";       json += rides[i].id;
    json += ",\"name\":\"";   json += jsonEscape(rides[i].name); json += "\"";
    json += ",\"isOpen\":";   json += rides[i].isOpen ? "true" : "false";
    json += ",\"waitTime\":"; json += rides[i].waitTime;
    json += ",\"enabled\":";  json += _cfgMgr.isRideEnabled(parkId, rides[i].id) ? "true" : "false";
    json += ",\"favorite\":"; json += _cfgMgr.isRideFavorite(parkId, rides[i].id) ? "true" : "false";
    json += "}";
  }
  json += "]";
  _server.send(200, "application/json", json);
}

void ConfigWebServer::handleApiConfig() {
  const RuntimeConfig& cfg = _cfgMgr.getConfig();
  DynamicJsonDocument doc(12288);
  doc["apiRefreshInterval"]    = cfg.apiRefreshInterval / 1000;
  doc["rotateInterval"]        = cfg.rotateInterval / 1000;
  doc["closedParkDisplayTime"] = cfg.closedParkDisplayTime / 1000;
  doc["timeUpdateInterval"]    = cfg.timeUpdateInterval / 1000;
  JsonArray ep = doc.createNestedArray("enabledParks");
  for (size_t i = 0; i < cfg.enabledParkIds.size(); i++) {
    JsonObject p = ep.createNestedObject();
    p["id"] = cfg.enabledParkIds[i]; p["name"] = cfg.enabledParkNames[i];
  }
  doc["currentParkId"] = cfg.enabledParkIds.size() > 0 ? cfg.enabledParkIds[0] : 0;
  if (cfg.rideFiltersJson.length() > 0) {
    DynamicJsonDocument fd(4096);
    if (!deserializeJson(fd, cfg.rideFiltersJson)) doc["rideFilters"] = fd.as<JsonObject>();
  }

  doc["brightness"]      = cfg.brightness;
  doc["quietEnabled"]    = cfg.quietHoursEnabled;
  doc["quietStart"]      = minutesToHHMM(cfg.quietStartMin);
  doc["quietEnd"]        = minutesToHHMM(cfg.quietEndMin);
  doc["quietBrightness"] = cfg.quietBrightness;
  doc["ledEnabled"]      = cfg.ledEnabled;
  doc["flipScreen"]      = cfg.flipScreen;
  doc["deviceTimezone"]  = cfg.deviceTimezone;
  doc["colorPalette"]    = cfg.colorPalette;

  JsonArray wt = doc.createNestedArray("waitThresholds");
  wt.add(cfg.waitTh1); wt.add(cfg.waitTh2); wt.add(cfg.waitTh3);
  JsonArray wc = doc.createNestedArray("waitColors");
  for (int i = 0; i < 5; i++) wc.add(hexColor(cfg.waitColors[i]));

  JsonObject ro = doc.createNestedObject("rideOptions");
  ro["sortMode"]       = cfg.sortMode;
  ro["favoritesFirst"] = cfg.favoritesFirst;
  ro["skipClosed"]     = cfg.skipClosedRides;
  ro["minWait"]        = cfg.minWaitMinutes;

  if (cfg.rideFavoritesJson.length() > 0) {
    DynamicJsonDocument fav(4096);
    if (!deserializeJson(fav, cfg.rideFavoritesJson)) doc["rideFavorites"] = fav.as<JsonObject>();
  }

  String json; serializeJson(doc, json);
  _server.send(200, "application/json", json);
}

void ConfigWebServer::handleSaveConfig() {
  if (!_server.hasArg("plain")) {
    _server.send(400, "application/json", "{\"success\":false,\"error\":\"No body\"}"); return;
  }
  DynamicJsonDocument doc(12288);
  if (deserializeJson(doc, _server.arg("plain"))) {
    _server.send(400, "application/json", "{\"success\":false,\"error\":\"JSON parse error\"}"); return;
  }
  FEED_WDT();

  unsigned long apiRefresh = (unsigned long)((int)doc["apiRefreshInterval"] * 1000UL);
  unsigned long rotate     = (unsigned long)((int)doc["rotateInterval"]     * 1000UL);
  unsigned long closedPark = (unsigned long)((int)doc["closedParkDisplayTime"] * 1000UL);
  unsigned long timeUpdate = (unsigned long)((int)doc["timeUpdateInterval"] * 1000UL);
  if (apiRefresh < 30000) apiRefresh = 30000;
  if (rotate     < 3000)  rotate     = 3000;
  if (closedPark < 5000)  closedPark = 5000;
  if (timeUpdate < 10000) timeUpdate = 10000;
  _cfgMgr.saveTimings(apiRefresh, rotate, closedPark, timeUpdate);
  FEED_WDT();

  JsonArray parksArr = doc["enabledParks"].as<JsonArray>();
  std::vector<int> parkIds; std::vector<String> parkNames;
  for (JsonObject p : parksArr) {
    int id = p["id"] | -1; const char* name = p["name"] | "";
    if (id > 0) { parkIds.push_back(id); parkNames.push_back(String(name)); }
  }
  DynamicJsonDocument parksDoc(4096);
  JsonArray parksOut = parksDoc.to<JsonArray>();
  for (size_t i = 0; i < parkIds.size(); i++) {
    JsonObject p = parksOut.createNestedObject();
    p["id"] = parkIds[i]; p["name"] = parkNames[i];
  }
  String parksJson; serializeJson(parksOut, parksJson);
  _cfgMgr.saveEnabledParks(parksJson);
  FEED_WDT();

  // Merge ride filters (and favorites — same shape). If the result exceeds
  // the NVS budget the save is rejected outright: truncating the JSON would
  // store an unparseable string that silently disables all filtering.
  JsonObject rideFiltersObj = doc["rideFilters"].as<JsonObject>();
  String filtersJson;
  if (!mergePerParkJson(_cfgMgr.getConfig().rideFiltersJson, rideFiltersObj,
                        parkIds, filtersJson)) {
    _server.send(400, "application/json",
                 "{\"success\":false,\"error\":\"Ride filters too large - use fewer per-ride selections\"}");
    return;
  }
  _cfgMgr.saveRideFilters(filtersJson);
  FEED_WDT();

  JsonObject rideFavsObj = doc["rideFavorites"].as<JsonObject>();
  String favoritesJson;
  if (!mergePerParkJson(_cfgMgr.getConfig().rideFavoritesJson, rideFavsObj,
                        parkIds, favoritesJson)) {
    _server.send(400, "application/json",
                 "{\"success\":false,\"error\":\"Too many favorites - unstar some rides\"}");
    return;
  }
  _cfgMgr.saveRideFavorites(favoritesJson);
  FEED_WDT();

  // Display settings + ride options. Missing keys keep the current values so
  // config files exported by older firmware still import cleanly.
  {
    const RuntimeConfig& cur = _cfgMgr.getConfig();
    int brt = doc.containsKey("brightness") ? (int)doc["brightness"] : cur.brightness;
    if (brt < 5)   brt = 5;      // never fully dark from the main slider
    if (brt > 100) brt = 100;
    bool qtEn  = doc.containsKey("quietEnabled") ? (bool)doc["quietEnabled"]
                                                 : cur.quietHoursEnabled;
    int qtSta  = doc.containsKey("quietStart")
                   ? hhmmToMinutes(doc["quietStart"].as<const char*>(), cur.quietStartMin)
                   : cur.quietStartMin;
    int qtEnd  = doc.containsKey("quietEnd")
                   ? hhmmToMinutes(doc["quietEnd"].as<const char*>(), cur.quietEndMin)
                   : cur.quietEndMin;
    int qtBrt  = doc.containsKey("quietBrightness") ? (int)doc["quietBrightness"]
                                                    : cur.quietBrightness;
    if (qtBrt < 0)   qtBrt = 0;
    if (qtBrt > 100) qtBrt = 100;
    bool ledEn = doc.containsKey("ledEnabled") ? (bool)doc["ledEnabled"]
                                               : cur.ledEnabled;
    bool flip  = doc.containsKey("flipScreen") ? (bool)doc["flipScreen"]
                                               : cur.flipScreen;
    String devTz = cur.deviceTimezone;
    if (doc.containsKey("deviceTimezone")) {
      const char* tz = doc["deviceTimezone"].as<const char*>();
      devTz = tz ? tz : "";
      if (devTz.length() > 40) devTz = cur.deviceTimezone;   // sanity cap
    }
    _cfgMgr.saveDisplaySettings((uint8_t)brt, qtEn, (uint16_t)qtSta,
                                (uint16_t)qtEnd, (uint8_t)qtBrt, ledEn, flip,
                                devTz);

    int pal = doc.containsKey("colorPalette") ? (int)doc["colorPalette"]
                                              : cur.colorPalette;
    if (pal < 0 || pal >= COLOR_PALETTE_COUNT) pal = 0;
    _cfgMgr.savePalette((uint8_t)pal);

    // Wait thresholds + level colours. Missing fields keep current values;
    // thresholds are clamped to 1..240 and forced strictly ascending.
    int t1 = cur.waitTh1, t2 = cur.waitTh2, t3 = cur.waitTh3;
    JsonArray wt = doc["waitThresholds"].as<JsonArray>();
    if (!wt.isNull() && wt.size() == 3) {
      t1 = wt[0] | t1; t2 = wt[1] | t2; t3 = wt[2] | t3;
    }
    if (t1 < 1)   t1 = 1;
    if (t1 > 238) t1 = 238;
    if (t2 <= t1) t2 = t1 + 1;
    if (t2 > 239) t2 = 239;
    if (t3 <= t2) t3 = t2 + 1;
    if (t3 > 240) t3 = 240;
    uint32_t cols[5];
    for (int i = 0; i < 5; i++) cols[i] = cur.waitColors[i];
    JsonArray wc = doc["waitColors"].as<JsonArray>();
    if (!wc.isNull() && wc.size() == 5) {
      for (int i = 0; i < 5; i++)
        cols[i] = parseHexColor(wc[i].as<const char*>(), cols[i]);
    }
    _cfgMgr.saveWaitConfig((uint8_t)t1, (uint8_t)t2, (uint8_t)t3, cols);

    JsonObject ro = doc["rideOptions"].as<JsonObject>();
    if (!ro.isNull()) {
      int sortMode = ro["sortMode"] | (int)cur.sortMode;
      if (sortMode != SORT_MODE_WAIT_DESC) sortMode = SORT_MODE_API_ORDER;
      bool favFirst   = ro["favoritesFirst"] | cur.favoritesFirst;
      bool skipClosed = ro["skipClosed"]     | cur.skipClosedRides;
      int  minWait    = ro["minWait"]        | (int)cur.minWaitMinutes;
      if (minWait < 0)   minWait = 0;
      if (minWait > 240) minWait = 240;
      _cfgMgr.saveRideOptions((uint8_t)sortMode, favFirst, skipClosed,
                              (uint8_t)minWait);
    }
  }
  FEED_WDT();

  _configUpdated = true;
  _server.send(200, "application/json", "{\"success\":true,\"message\":\"Configuration saved\"}");
}

void ConfigWebServer::handleFactoryReset() {
  _cfgMgr.factoryReset();  // also wipes WiFi credentials (shared namespace)
  _server.send(200, "application/json",
               "{\"success\":true,\"message\":\"Factory reset - restarting\"}");
  _pendingFactoryReset = true;  // handleClient() restarts after the response
}

void ConfigWebServer::handleNotFound() {
  _server.send(404, "text/plain", "Not found");
}

String ConfigWebServer::buildConfigPage() {
  String html;
  html.reserve(40000);
  for (size_t i = 0; CONFIG_HTML[i] != '\0'; i++) html += (char)pgm_read_byte(&CONFIG_HTML[i]);
  return html;
}
