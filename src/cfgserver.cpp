#include "cfgserver.h"
#include "config.h"
#include <ArduinoJson.h>

#if __has_include(<esp_task_wdt.h>)
#include <esp_task_wdt.h>
#define FEED_WDT() esp_task_wdt_reset()
#else
#define FEED_WDT() ((void)0)
#endif

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
</style>
</head>
<body>
<div class="container">
  <div class="hero">
    <h1>&#127906; QueueWatch</h1>
    <p>Live theme-park wait times on your desk. Configure parks, rides &amp; timing below.</p>
    <div class="status"><span class="dot"></span><span id="statusText">Device connected</span></div>
  </div>
  <form id="configForm" onsubmit="return saveConfig(event)">

  <div class="section">
    <h2><span class="ico">&#9201;</span> Timing</h2>
    <p class="hint">How often the device refreshes live data and rotates through rides.</p>
    <div class="grid">
      <div class="field"><label>API refresh</label><div class="in"><input type="number" id="api_int" min="30" max="3600" step="1" required><span class="unit">sec</span></div></div>
      <div class="field"><label>Ride rotate</label><div class="in"><input type="number" id="rot_int" min="3" max="300" step="1" required><span class="unit">sec</span></div></div>
      <div class="field"><label>Closed-park display</label><div class="in"><input type="number" id="closed_int" min="5" max="300" step="1" required><span class="unit">sec</span></div></div>
      <div class="field"><label>Clock update</label><div class="in"><input type="number" id="time_int" min="10" max="600" step="1" required><span class="unit">sec</span></div></div>
    </div>
  </div>

  <div class="section">
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

  <div class="section">
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
  <div class="section" style="border-color:#5a2440">
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
let allParks=[];let allRides=[];let currentParkId=0;let rideFilterCache={};
const $=id=>document.getElementById(id);

window.addEventListener('DOMContentLoaded',async()=>{
  try{const res=await fetch('/api/config');const cfg=await res.json();
    $('api_int').value=cfg.apiRefreshInterval;
    $('rot_int').value=cfg.rotateInterval;
    $('closed_int').value=cfg.closedParkDisplayTime;
    $('time_int').value=cfg.timeUpdateInterval;
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
  rideFilterCache[currentParkId]=(enabledIds.length===allRides.length)?null:enabledIds;}

async function fetchRidesForPark(parkId){currentParkId=parkId;
  const c=$('rideList');c.innerHTML='<p class="empty"><span class="spinner"></span> Loading rides...</p>';
  $('rideStats').innerHTML='';$('rideTools').style.display='none';
  try{const res=await fetch('/api/rides?parkId='+parkId);allRides=await res.json();
    if(rideFilterCache.hasOwnProperty(parkId)){const cached=rideFilterCache[parkId];
      for(const ride of allRides)ride.enabled=cached===null?true:cached.includes(ride.id);}
    renderRideList();
  }catch(e){c.innerHTML='<p class="empty" style="color:var(--red)">Failed to load rides.</p>';}}

function waitBadge(r){
  if(r.isOpen===false)return '<span class="badge b-teal">CLOSED</span>';
  if(r.waitTime<0)return '<span class="badge b-muted">n/a</span>';
  const cls=r.waitTime<=15?'b-green':r.waitTime<=30?'b-amber':r.waitTime<=45?'b-orange':'b-red';
  return '<span class="badge '+cls+'">'+r.waitTime+'<small>min</small></span>';}

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
    html+='<div class="item"><label class="switch"><input type="checkbox" id="ride_'+ride.id+'" '+(ride.enabled?'checked':'')+' onchange="toggleRide('+ride.id+')"><span class="slider"></span></label><span class="name"><span class="rid">#'+ride.id+'</span> '+escapeHtml(ride.name)+'</span>'+waitBadge(ride)+'</div>';
  }
  c.innerHTML=html;}

function toggleRide(id){const ride=allRides.find(r=>r.id===id);if(ride)ride.enabled=!ride.enabled;}
function selectAllRides(select){for(const ride of allRides)ride.enabled=select;renderRideList();}

async function saveConfig(event){event.preventDefault();saveCurrentRideFilterState();
  const btn=$('saveBtn');btn.disabled=true;btn.innerHTML='<span class="spinner"></span> Saving...';
  const enabledParks=allParks.filter(p=>p.enabled).map(p=>({id:p.id,name:p.name}));
  const rideFilters={};for(const park of enabledParks){const cached=rideFilterCache[park.id];if(cached===undefined)continue;rideFilters[park.id]=(cached&&cached.length>0)?cached:null;}
  const body={apiRefreshInterval:parseInt($('api_int').value),rotateInterval:parseInt($('rot_int').value),closedParkDisplayTime:parseInt($('closed_int').value),timeUpdateInterval:parseInt($('time_int').value),enabledParks,rideFilters};
  try{const res=await fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)});
    const result=await res.json();
    if(result.success)toast('&#10003; Saved! Display cycle restarting...','success');
    else toast('Error: '+(result.error||'unknown'),'error');
  }catch(e){toast('Failed to save configuration','error');}
  btn.disabled=false;btn.innerHTML='&#10003; Save &amp; Restart Cycle';return false;}

let toastTimer;
function toast(text,type){const t=$('toast');t.className='toast '+(type||'info');t.innerHTML=text;
  requestAnimationFrame(()=>t.classList.add('show'));
  clearTimeout(toastTimer);toastTimer=setTimeout(()=>t.classList.remove('show'),3200);}

function escapeHtml(str){return String(str).replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/"/g,'&quot;').replace(/'/g,'&#039;');}
</script>
</body>
</html>
)rawliteral";

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
    json += "}";
  }
  json += "]";
  _server.send(200, "application/json", json);
}

void ConfigWebServer::handleApiConfig() {
  const RuntimeConfig& cfg = _cfgMgr.getConfig();
  DynamicJsonDocument doc(8192);
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

  // Merge ride filters. Start from the existing stored filters as a WRITABLE
  // object so parks the user didn't open this session keep their filter, then
  // overlay the incoming ones. A null value for a park means "clear its filter"
  // (all rides re-enabled), so future rides are shown too.
  JsonObject rideFiltersObj = doc["rideFilters"].as<JsonObject>();
  DynamicJsonDocument mergedDoc(8192);
  JsonObject merged;
  String existingFilters = _cfgMgr.getConfig().rideFiltersJson;
  if (existingFilters.length() > 0 && !deserializeJson(mergedDoc, existingFilters)
      && mergedDoc.is<JsonObject>()) {
    merged = mergedDoc.as<JsonObject>();      // reuse existing (writable)
  } else {
    mergedDoc.clear();
    merged = mergedDoc.to<JsonObject>();      // fresh, writable object
  }

  if (!rideFiltersObj.isNull()) {
    for (JsonPair kv : rideFiltersObj) {
      if (kv.value().isNull()) merged.remove(kv.key());   // clear this park's filter
      else                     merged[kv.key()] = kv.value();
    }
  }
  // Remove orphaned filters
  std::vector<String> keysToRemove;
  for (JsonPair kv : merged) {
    int parkId = atoi(kv.key().c_str());
    bool stillEnabled = false;
    for (size_t i = 0; i < parkIds.size(); i++) { if (parkIds[i] == parkId) { stillEnabled = true; break; } }
    if (!stillEnabled) keysToRemove.push_back(String(kv.key().c_str()));
  }
  for (size_t i = 0; i < keysToRemove.size(); i++) merged.remove(keysToRemove[i].c_str());

  String filtersJson;
  if (merged.size() > 0) {
    serializeJson(merged, filtersJson);
    if (filtersJson.length() > 1900) filtersJson = filtersJson.substring(0, 1900);
  }
  _cfgMgr.saveRideFilters(filtersJson);
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
  html.reserve(24000);
  for (size_t i = 0; CONFIG_HTML[i] != '\0'; i++) html += (char)pgm_read_byte(&CONFIG_HTML[i]);
  return html;
}
