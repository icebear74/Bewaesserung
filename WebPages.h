#pragma once
#include <Arduino.h>

// ─── Common Header ────────────────────────────────────────────────────────────

const char HTML_HEADER[] PROGMEM = R"rawhtml(<!DOCTYPE html>
<html lang="de">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Bewässerung</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:Arial,sans-serif;background:#f5f5f5;color:#333}
.navbar{background:#1a6b3c;padding:10px 16px;display:flex;align-items:center;flex-wrap:wrap;gap:8px}
.navbar a{color:#fff;text-decoration:none;padding:6px 12px;border-radius:4px;font-size:14px;font-weight:bold}
.navbar a:hover{background:rgba(255,255,255,0.2)}
.navbar .brand{color:#fff;font-size:18px;font-weight:bold;margin-right:8px}
.container{max-width:800px;margin:24px auto;padding:0 16px}
.card{background:#fff;border-radius:8px;box-shadow:0 2px 6px rgba(0,0,0,0.1);padding:24px;margin-bottom:20px}
h1{font-size:22px;margin-bottom:16px;color:#1a6b3c}
h2{font-size:18px;margin-bottom:12px;color:#333}
table{width:100%;border-collapse:collapse}
th,td{text-align:left;padding:10px 12px;border-bottom:1px solid #eee}
th{background:#f0f0f0;font-weight:bold}
label{display:block;margin-bottom:6px;font-weight:bold;font-size:14px}
input[type=text],input[type=password],input[type=number],select{
  width:100%;padding:9px 12px;border:1px solid #ccc;border-radius:4px;font-size:14px;margin-bottom:14px}
input[type=checkbox]{width:auto;margin-right:8px}
.btn{display:inline-block;padding:10px 22px;background:#1a6b3c;color:#fff;
  border:none;border-radius:4px;font-size:15px;cursor:pointer;text-decoration:none}
.btn:hover{background:#145530}
.btn-danger{background:#dc3545}  /* reserved for destructive actions (Phase 2: delete schedule) */
.btn-danger:hover{background:#b02a37}
.alert-danger{background:#dc3545;color:#fff;padding:12px;border-radius:4px;margin-bottom:16px;font-weight:bold}
.alert-warning{background:#ffc107;color:#333;padding:12px;border-radius:4px;margin-bottom:16px;font-weight:bold}
.alert-info{background:#17a2b8;color:#fff;padding:12px;border-radius:4px;margin-bottom:16px;font-size:13px}
.form-row{display:flex;gap:12px;flex-wrap:wrap}
.form-col{flex:1;min-width:200px}
#map{height:300px;border-radius:4px;margin-bottom:14px;border:1px solid #ccc}
</style>
</head>
<body>
<nav class="navbar">
  <span class="brand">🌿 Bewässerung</span>
  <a href="/status">Status</a>
  <a href="/config_wifi">WLAN</a>
  <a href="/config_time">Zeit</a>
  <a href="/config_location">Standort</a>
  <a href="/config_hardware">Hardware</a>
  <a href="/config_watering">Bewässerung</a>
</nav>
<div class="container">
)rawhtml";

// ─── Footer ───────────────────────────────────────────────────────────────────

const char HTML_FOOTER[] PROGMEM = R"rawhtml(
</div>
</body>
</html>
)rawhtml";

// ─── Status Page ──────────────────────────────────────────────────────────────
// Placeholders: {ds3231_warning} {watering_locked_warning} {wifi_status}
//               {ip_address} {time_str} {uptime} {state_str}
//               {ds3231_status} {offline_mode} {rtc_temp}

const char HTML_STATUS_PAGE[] PROGMEM = R"rawhtml(
<div class="card">
  <h1>📊 Systemstatus</h1>
  {ds3231_warning}
  {watering_locked_warning}
  <table>
    <tr><th>Parameter</th><th>Wert</th></tr>
    <tr><td>Systemzustand</td><td><b>{state_str}</b></td></tr>
    <tr><td>WLAN-Status</td><td>{wifi_status}</td></tr>
    <tr><td>IP-Adresse</td><td>{ip_address}</td></tr>
    <tr><td>Lokale Zeit</td><td>{time_str}</td></tr>
    <tr><td>Betriebszeit</td><td>{uptime}</td></tr>
    <tr><td>DS3231 RTC</td><td>{ds3231_status}</td></tr>
    <tr><td>RTC Temperatur</td><td>{rtc_temp}</td></tr>
    <tr><td>Offline-Modus</td><td>{offline_mode}</td></tr>
  </table>
</div>
)rawhtml";

// ─── WiFi Config Page ─────────────────────────────────────────────────────────

const char HTML_WIFI_PAGE[] PROGMEM = R"rawhtml(
<div class="card">
  <h1>📶 WLAN-Konfiguration</h1>
  <div class="alert-info">
    ℹ️ Nach dem Speichern der WLAN-Einstellungen startet das Gerät neu und verbindet sich mit dem neuen Netzwerk.
  </div>
  <form method="POST" action="/save_wifi">
    <label for="ssid">WLAN-Name (SSID)</label>
    <input type="text" id="ssid" name="ssid" value="{ssid}" placeholder="Ihr WLAN-Name" maxlength="63">
    <label for="password">Passwort</label>
    <input type="password" id="password" name="password" value="{password}" placeholder="WLAN-Passwort" maxlength="63">
    <label for="hostname">Gerätename (Hostname)</label>
    <input type="text" id="hostname" name="hostname" value="{hostname}" placeholder="Bewaesserung" maxlength="31">
    <div style="margin-top:8px">
      <button class="btn" type="submit">💾 Speichern &amp; Neu starten</button>
    </div>
  </form>
</div>
)rawhtml";

// ─── Time Config Page ─────────────────────────────────────────────────────────

const char HTML_TIME_PAGE[] PROGMEM = R"rawhtml(
<div class="card">
  <h1>🕐 Zeit &amp; Zeitzone</h1>
  <form method="POST" action="/save_time">
    <label for="timezone">Zeitzone</label>
    <select id="timezone" name="timezone">
      <option value="UTC" {tz_UTC}>UTC</option>
      <option value="CET-1CEST,M3.5.0,M10.5.0/3" {tz_CET}>Europa/Berlin, Wien, Zürich (CET/CEST)</option>
      <option value="GMT0BST,M3.5.0/1,M10.5.0" {tz_GMT}>Europa/London, Dublin (GMT/BST)</option>
      <option value="EET-2EEST,M3.5.0/3,M10.5.0/4" {tz_EET}>Europa/Athen, Helsinki (EET/EEST)</option>
      <option value="WET0WEST,M3.5.0/1,M10.5.0" {tz_WET}>Europa/Lissabon, Canaren (WET/WEST)</option>
      <option value="EST5EDT,M3.2.0,M11.1.0" {tz_EST}>Amerika/New York (EST/EDT)</option>
      <option value="CST6CDT,M3.2.0,M11.1.0" {tz_CST}>Amerika/Chicago (CST/CDT)</option>
      <option value="MST7MDT,M3.2.0,M11.1.0" {tz_MST}>Amerika/Denver (MST/MDT)</option>
      <option value="PST8PDT,M3.2.0,M11.1.0" {tz_PST}>Amerika/Los Angeles (PST/PDT)</option>
      <option value="AEST-10AEDT,M10.1.0,M4.1.0/3" {tz_AEST}>Australien/Sydney (AEST/AEDT)</option>
      <option value="JST-9" {tz_JST}>Asien/Tokio (JST)</option>
      <option value="CST-8" {tz_CST8}>Asien/Shanghai, Hongkong (CST)</option>
      <option value="IST-5:30" {tz_IST}>Asien/Kolkata (IST)</option>
    </select>
    <label for="ntpServer">NTP-Server</label>
    <input type="text" id="ntpServer" name="ntpServer" value="{ntpServer}" placeholder="pool.ntp.org" maxlength="63">
    <div style="margin-top:8px">
      <button class="btn" type="submit">💾 Speichern</button>
    </div>
  </form>
</div>
)rawhtml";

// ─── Location Config Page ─────────────────────────────────────────────────────

const char HTML_LOCATION_PAGE[] PROGMEM = R"rawhtml(
<link rel="stylesheet" href="https://unpkg.com/leaflet@1.9.4/dist/leaflet.css"/>
<script src="https://unpkg.com/leaflet@1.9.4/dist/leaflet.js"></script>
<div class="card">
  <h1>&#128205; Standort</h1>
  <div style="display:flex;gap:8px;margin-bottom:14px">
    <input type="text" id="locSearch" placeholder="Ort suchen (z.B. M&#252;nchen, Balkon 12. Str.)" style="flex:1;margin:0">
    <button type="button" class="btn" onclick="searchLocation()" style="white-space:nowrap">&#128269; Suchen</button>
  </div>
  <div id="searchStatus" style="font-size:13px;color:#666;margin-bottom:8px"></div>
  <form method="POST" action="/save_location" id="locForm">
    <label>Standort auf Karte w&#228;hlen (Marker verschieben oder Karte anklicken)</label>
    <div id="map"></div>
    <div class="form-row">
      <div class="form-col">
        <label for="latitude">Breitengrad</label>
        <input type="number" id="latitude" name="latitude" value="{latitude}" step="0.0001" min="-90" max="90">
      </div>
      <div class="form-col">
        <label for="longitude">L&#228;ngengrad</label>
        <input type="number" id="longitude" name="longitude" value="{longitude}" step="0.0001" min="-180" max="180">
      </div>
    </div>
    <label for="locationName">Ortsname (optional)</label>
    <input type="text" id="locationName" name="locationName" value="{locationName}" placeholder="z.B. Garten M&#252;nchen" maxlength="63">
    <div style="margin-top:8px">
      <button class="btn" type="submit">&#128190; Speichern</button>
    </div>
  </form>
</div>
<script>
var map, marker;
(function(){
  var lat = parseFloat(document.getElementById('latitude').value) || 48.1351;
  var lng = parseFloat(document.getElementById('longitude').value) || 11.5820;
  map = L.map('map').setView([lat, lng], 13);
  L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png',{
    attribution:'&#169; OpenStreetMap',maxZoom:19}).addTo(map);
  marker = L.marker([lat,lng],{draggable:true}).addTo(map);
  marker.on('dragend',function(e){
    var pos = e.target.getLatLng();
    document.getElementById('latitude').value = pos.lat.toFixed(6);
    document.getElementById('longitude').value = pos.lng.toFixed(6);
  });
  map.on('click',function(e){
    marker.setLatLng(e.latlng);
    document.getElementById('latitude').value = e.latlng.lat.toFixed(6);
    document.getElementById('longitude').value = e.latlng.lng.toFixed(6);
  });
})();
function searchLocation(){
  var q = document.getElementById('locSearch').value.trim();
  if(!q) return;
  var st = document.getElementById('searchStatus');
  st.textContent = 'Suche...';
  // Note: browser's User-Agent is sent automatically, satisfying Nominatim's identification requirement
  fetch('https://nominatim.openstreetmap.org/search?q='+encodeURIComponent(q)+'&format=json&limit=1&accept-language=de')
  .then(function(r){return r.json();})
  .then(function(data){
    if(data && data.length > 0){
      var lat = parseFloat(data[0].lat);
      var lon = parseFloat(data[0].lon);
      document.getElementById('latitude').value = lat.toFixed(6);
      document.getElementById('longitude').value = lon.toFixed(6);
      var displayName = data[0].name || (data[0].display_name||'').split(',')[0];
      if(!document.getElementById('locationName').value)
        document.getElementById('locationName').value = displayName;
      marker.setLatLng([lat,lon]);
      map.setView([lat,lon],13);
      // Use textContent to safely display potentially untrusted API data
      st.textContent = '\u2713 Gefunden: ' + (data[0].display_name||'');
      st.style.color = '#1a6b3c';
    } else {
      st.textContent = '\u2717 Ort nicht gefunden.';
      st.style.color = '#dc3545';
    }
  })
  .catch(function(){
    st.textContent = '\u2717 Suche fehlgeschlagen (Internetverbindung pr\u00fcfen).';
    st.style.color = '#dc3545';
  });
}
document.getElementById('locSearch').addEventListener('keydown', function(e){
  if(e.key==='Enter'){e.preventDefault();searchLocation();}
});
</script>
)rawhtml";

// ─── Hardware Config Page ─────────────────────────────────────────────────────
// Tokens: {expanderCount} {expander_rows_html} {expanders_json} {pumpCount} {pump_rows_html}

const char HTML_HARDWARE_PAGE[] PROGMEM = R"rawhtml(
<div class="card">
  <h1>&#9881;&#65039; Hardware-Konfiguration</h1>
  <div class="alert-info">
    &#128161; &#196;nderungen werden sofort &#252;bernommen (kein Neustart erforderlich).
    Test-Buttons aktivieren die Pumpe kurzzeitig.
  </div>
  <form method="POST" action="/save_hardware" id="hwForm">

    <h2 style="margin-top:18px;margin-bottom:6px;font-size:1.05em;color:#1a6b3c">
      &#128268; Optionale Hardware (I2C Expander)
    </h2>
    <div class="alert-info" style="margin-bottom:8px">
      Hier werden I2C GPIO-Expander-Chips definiert (PCF8574 / PCF8575).
      Pumpen k&#246;nnen einen dieser Expander als Ausgangstyp verwenden.
    </div>
    <label for="expCount">Anzahl Expander (0&#x2013;4)</label>
    <input type="number" id="expCount" name="expCount" value="{expanderCount}"
           min="0" max="4" onchange="rebuildExpanders(this.value)">
    <div id="expanderRows">{expander_rows_html}</div>

    <h2 style="margin-top:22px;margin-bottom:6px;font-size:1.05em;color:#1a6b3c">
      &#128167; Pumpen
    </h2>
    <label for="pumpCount">Anzahl Pumpen (0&#x2013;8)</label>
    <input type="number" id="pumpCount" name="pumpCount" value="{pumpCount}"
           min="0" max="8" onchange="rebuildPumps(this.value)">
    <div id="pumpRows">{pump_rows_html}</div>

    <div style="margin-top:14px">
      <button class="btn" type="submit">&#128190; Speichern</button>
    </div>
  </form>
</div>
<script>
var _exp={expanders_json};
function _expLabel(e){return e.name+' ('+e.addr+', '+(e.chipType===1?'PCF8575':'PCF8574')+')';}
function _expOptions(selIdx){
  if(!_exp.length) return '<option value="0" disabled>&#x26A0; Kein Expander – zuerst oben anlegen</option>';
  var s='';
  for(var i=0;i<_exp.length;i++){
    s+='<option value="'+i+'"'+(i===selIdx?' selected':'')+'>'+_expLabel(_exp[i])+'</option>';
  }
  return s;
}
function toggleOutType(i,v){
  document.getElementById('gpio'+i).style.display=v==='0'?'block':'none';
  document.getElementById('i2c'+i).style.display=v==='1'?'block':'none';
}
function onExpanderChange(pumpIdx,sel){
  var idx=parseInt(sel.value)||0;
  var maxChan=(_exp[idx]&&_exp[idx].chipType===1)?15:7;
  var ch=document.getElementById('chan'+pumpIdx);
  if(ch){ch.max=maxChan;ch.placeholder='0-'+maxChan;}
}
// ── Expander rows ──────────────────────────────────────────────────────────────
function mkExpanderRow(i,d){
  d=d||{};
  var e=d.enabled!==false?'checked':'';
  var nm=d.name||'';
  var t0=d.chipType===0?'selected':''; var t1=d.chipType===1?'selected':'';
  var addr=d.addr!=null?d.addr:32;
  return '<div class="pump-entry" style="border:1px solid #cce0ff;padding:10px;margin-bottom:8px;border-radius:4px;background:#f5f9ff">'
  +'<b>Expander '+(i+1)+'</b>'
  +'<div class="form-row" style="margin-top:6px">'
  +'<div class="form-col"><label><input type="checkbox" name="ex'+i+'_enabled" '+e+'> Aktiv</label></div>'
  +'<div class="form-col"><label>Name</label><input type="text" name="ex'+i+'_name" value="'+nm+'" maxlength="31"></div>'
  +'</div><div class="form-row">'
  +'<div class="form-col"><label>Chiptyp</label><select name="ex'+i+'_type">'
  +'<option value="0" '+t0+'>PCF8574 (8 Ports, 0x20&#x2013;0x27)</option>'
  +'<option value="1" '+t1+'>PCF8575 (16 Ports, 0x20&#x2013;0x27)</option>'
  +'</select></div>'
  +'<div class="form-col"><label>I2C-Adresse (dez., 32=0x20 &#x2026; 39=0x27)</label>'
  +'<input type="number" name="ex'+i+'_addr" value="'+addr+'" min="32" max="39"></div>'
  +'</div></div>';
}
function getVal(div,sel){var el=div.querySelector(sel);return el?el.value:'';}
function getChk(div,sel){var el=div.querySelector(sel);return el?el.checked:false;}
function rebuildExpanders(n){
  n=parseInt(n)||0;
  var div=document.getElementById('expanderRows');
  var html='';
  for(var i=0;i<n;i++){
    var nmEl=div.querySelector('[name="ex'+i+'_name"]');
    if(nmEl){
      html+=mkExpanderRow(i,{
        enabled:getChk(div,'[name="ex'+i+'_enabled"]'),
        name:getVal(div,'[name="ex'+i+'_name"]'),
        chipType:parseInt(getVal(div,'[name="ex'+i+'_type"]')||'0'),
        addr:parseInt(getVal(div,'[name="ex'+i+'_addr"]')||'32')
      });
    }else{
      html+=mkExpanderRow(i,{enabled:true,addr:32});
    }
  }
  div.innerHTML=html;
  // Keep _exp in sync so pump dropdowns rebuild correctly
  _exp=[];
  for(var j=0;j<n;j++){
    var nd=document.getElementById('expanderRows');
    var ct=parseInt(getVal(nd,'[name="ex'+j+'_type"]')||'0');
    _exp.push({name:getVal(nd,'[name="ex'+j+'_name"]'),chipType:ct,
               maxChan:ct===1?15:7,addr:'0x'+('0'+parseInt(getVal(nd,'[name="ex'+j+'_addr"]')||'32').toString(16)).slice(-2).toUpperCase()});
  }
}
// ── Pump rows ──────────────────────────────────────────────────────────────────
function mkRow(i,d){
  d=d||{};
  var e=d.enabled!==false?'checked':'';
  var inv=d.invertLogic?'checked':'';
  var t=d.outputType||0;
  var t0=t===0?'selected':''; var t1=t===1?'selected':'';
  var nm=d.name||''; var nt=d.notes||'';
  var pin=d.pin!=null?d.pin:-1; var mr=d.maxRuntimeSec||300;
  var expIdx=d.expanderIndex||0;
  var chan=d.i2cChannel!=null?d.i2cChannel:0;
  var maxChan=(_exp[expIdx]&&_exp[expIdx].chipType===1)?15:7;
  var gpioDisp=t===0?'block':'none';
  var i2cDisp=t===1?'block':'none';
  return '<div class="pump-entry" style="border:1px solid #ddd;padding:10px;margin-bottom:10px;border-radius:4px">'
  +'<b>Pumpe '+(i+1)+'</b>'
  +'<div class="form-row" style="margin-top:6px">'
  +'<div class="form-col"><label><input type="checkbox" name="p'+i+'_enabled" '+e+'> Aktiv</label></div>'
  +'<div class="form-col"><label>Name</label><input type="text" name="p'+i+'_name" value="'+nm+'" maxlength="31"></div>'
  +'</div><div class="form-row">'
  +'<div class="form-col"><label>Ausgangstyp</label><select name="p'+i+'_type" onchange="toggleOutType('+i+',this.value)">'
  +'<option value="0" '+t0+'>Direkt-GPIO</option>'
  +'<option value="1" '+t1+'>I2C Expander (PCF8574/8575)</option>'
  +'</select></div></div>'
  +'<div id="gpio'+i+'" style="display:'+gpioDisp+'">'
  +'<div class="form-row"><div class="form-col"><label>GPIO-Pin (-1 = inaktiv)</label>'
  +'<input type="number" name="p'+i+'_pin" value="'+pin+'" min="-1" max="39"></div></div></div>'
  +'<div id="i2c'+i+'" style="display:'+i2cDisp+'">'
  +'<div class="form-row">'
  +'<div class="form-col"><label>Expander</label>'
  +'<select name="p'+i+'_expander" onchange="onExpanderChange('+i+',this)">'+_expOptions(expIdx)+'</select></div>'
  +'<div class="form-col"><label>Kanal (0&#x2013;'+maxChan+')</label>'
  +'<input type="number" name="p'+i+'_i2cChan" id="chan'+i+'" value="'+chan+'" min="0" max="'+maxChan+'"></div>'
  +'</div></div>'
  +'<div class="form-row" style="margin-top:4px">'
  +'<div class="form-col"><label><input type="checkbox" name="p'+i+'_invert" '+inv+'> Aktiv-LOW (invertiert)</label></div>'
  +'<div class="form-col"><label>Max. Test-Laufzeit (s)</label>'
  +'<input type="number" name="p'+i+'_maxRuntime" value="'+mr+'" min="1" max="3600"></div></div>'
  +'<label>Notizen</label><input type="text" name="p'+i+'_notes" value="'+nt+'" maxlength="63">'
  +'<div style="margin-top:8px">'
  +'<button type="button" onclick="testRelay('+i+',\'on\')" style="margin-right:4px;padding:5px 14px;background:#1a6b3c;color:#fff;border:none;border-radius:4px;cursor:pointer">&#9654; Test EIN</button>'
  +'<button type="button" onclick="testRelay('+i+',\'off\')" style="padding:5px 14px;background:#dc3545;color:#fff;border:none;border-radius:4px;cursor:pointer">&#9646; Test AUS</button>'
  +' <span id="ts'+i+'" style="font-size:12px;color:#555"></span>'
  +'</div></div>';
}
function rebuildPumps(n){
  n=parseInt(n)||0;
  var div=document.getElementById('pumpRows');
  var html='';
  for(var i=0;i<n;i++){
    var typeEl=div.querySelector('[name="p'+i+'_type"]');
    if(typeEl){
      var t=parseInt(typeEl.value)||0;
      html+=mkRow(i,{
        enabled:getChk(div,'[name="p'+i+'_enabled"]'),
        name:getVal(div,'[name="p'+i+'_name"]'),
        outputType:t,
        pin:parseInt(getVal(div,'[name="p'+i+'_pin"]')||'-1'),
        expanderIndex:parseInt(getVal(div,'[name="p'+i+'_expander"]')||'0'),
        i2cChannel:parseInt(getVal(div,'[name="p'+i+'_i2cChan"]')||'0'),
        invertLogic:getChk(div,'[name="p'+i+'_invert"]'),
        maxRuntimeSec:parseInt(getVal(div,'[name="p'+i+'_maxRuntime"]')||'300'),
        notes:getVal(div,'[name="p'+i+'_notes"]')
      });
    }else{
      html+=mkRow(i,{pin:-1,maxRuntimeSec:300,enabled:true});
    }
  }
  div.innerHTML=html;
}
function testRelay(idx,action){
  var sp=document.getElementById('ts'+idx);
  sp.textContent='...';
  fetch('/relay_test',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'relay='+idx+'&action='+action})
  .then(function(r){return r.json();})
  .then(function(d){sp.textContent=d.msg;sp.style.color=d.ok?'#1a6b3c':'#dc3545';})
  .catch(function(){sp.textContent='Verbindungsfehler';sp.style.color='#dc3545';});
}
</script>
)rawhtml";

// ─── Watering Config Page ─────────────────────────────────────────────────────
// Tokens: {watering_status} {relayCount} {relay_names_json} {nextIdx} {entry_rows_html}

const char HTML_WATERING_PAGE[] PROGMEM = R"rawhtml(
<div class="card">
  <h1>&#128167; Bew&#228;sserungsplan</h1>
  {watering_status}
  <form method="POST" action="/save_watering" id="wf">
    <div id="entries">{entry_rows_html}</div>
    <div style="margin-top:10px;display:flex;gap:8px">
      <button type="button" class="btn" onclick="addEntry()" style="background:#17a2b8">+ Eintrag hinzuf&#252;gen</button>
      <button class="btn" type="submit">&#128190; Speichern</button>
    </div>
  </form>
</div>
<script>
var relayCount={relayCount};
var relayNames={relay_names_json};
var nextIdx={nextIdx};
var dayL=['Mo','Di','Mi','Do','Fr','Sa','So'];
function pad(n){return n<10?'0'+n:''+n;}
function daysHtml(days,i){
  var s='';
  for(var d=0;d<7;d++){
    var c=(days&(1<<d))?'checked':'';
    s+='<label style="margin-right:7px"><input type="checkbox" name="e'+i+'_d'+d+'" '+c+'> '+dayL[d]+'</label>';
  }
  return s;
}
function relayOpts(sel){
  var s='';
  for(var r=0;r<relayCount;r++){
    s+='<option value="'+r+'"'+(sel===r?' selected':'')+'>'+(relayNames[r]||'Pumpe '+(r+1))+'</option>';
  }
  return s;
}
function mkEntry(i,d){
  d=d||{};
  var days=d.days!=null?d.days:0x7F;
  var ac=d.active!==false?'checked':'';
  var t=pad(d.hour||6)+':'+pad(d.minute||0);
  return '<div class="pump-entry" id="e'+i+'" style="border:1px solid #ddd;padding:10px;margin-bottom:8px;border-radius:4px">'
  +'<div class="form-row">'
  +'<div class="form-col"><label>Pumpe</label><select name="e'+i+'_relay">'+relayOpts(d.relay||0)+'</select></div>'
  +'<div class="form-col"><label>Startzeit</label><input type="time" name="e'+i+'_time" value="'+t+'"></div>'
  +'<div class="form-col"><label>Dauer (s)</label><input type="number" name="e'+i+'_duration" value="'+(d.durationSec||120)+'" min="1" max="7200"></div>'
  +'</div>'
  +'<div style="margin-top:6px">'+daysHtml(days,i)+'</div>'
  +'<div style="margin-top:6px">'
  +'<label><input type="checkbox" name="e'+i+'_active" '+ac+'> Aktiv</label>'
  +' <button type="button" onclick="delEntry('+i+')" style="margin-left:12px;padding:3px 10px;background:#dc3545;color:#fff;border:none;border-radius:4px;cursor:pointer">&#10005; L&#246;schen</button>'
  +'</div></div>';
}
function addEntry(){
  if(relayCount===0){alert('Bitte zuerst Pumpen in der Hardware-Konfiguration anlegen.');return;}
  document.getElementById('entries').insertAdjacentHTML('beforeend',mkEntry(nextIdx++,{}));
}
function delEntry(id){
  var el=document.getElementById('e'+id);
  if(el)el.remove();
}
</script>
)rawhtml";

// ─── DS3231 Warning HTML ──────────────────────────────────────────────────────

const char HTML_DS3231_WARNING[] PROGMEM = R"rawhtml(
<div class="alert-danger">
  ⚠️ DS3231 RTC nicht erkannt! Zeit kann ohne WLAN nicht persistent gespeichert werden.
  System befindet sich im eingeschränkten Notbetrieb!
</div>
)rawhtml";

// ─── Watering Locked Warning HTML ────────────────────────────────────────────

const char HTML_WATERING_LOCKED_WARNING[] PROGMEM = R"rawhtml(
<div class="alert-warning">
  ⚠️ Kein Bewässerungsplan konfiguriert! Relais sind gesperrt.
  Bitte konfigurieren Sie mindestens einen Bewässerungseintrag und die Hardware.
</div>
)rawhtml";

// ─── 404 Not Found Page ───────────────────────────────────────────────────────

const char HTML_404_PAGE[] PROGMEM = R"rawhtml(
<div class="card">
  <h1>404 &#8211; Seite nicht gefunden</h1>
  <p>Die angeforderte Seite existiert nicht.</p>
  <a class="btn" href="/" style="margin-top:12px">Zur Startseite</a>
</div>
)rawhtml";

// ─── Error Page ───────────────────────────────────────────────────────────────
// Tokens: {error_msg} {back_url}

const char HTML_ERROR_PAGE[] PROGMEM = R"rawhtml(
<div class="card">
  <h1>&#10007; Fehler</h1>
  <div class="alert-danger">{error_msg}</div>
  <a class="btn" href="{back_url}" style="margin-top:12px">&#8592; Zur&#252;ck</a>
</div>
)rawhtml";

// ─── Save Confirmation Pages ──────────────────────────────────────────────────

const char HTML_SAVED_RESTART[] PROGMEM = R"rawhtml(
<div class="card">
  <h1>&#10003; Gespeichert</h1>
  <p>Konfiguration gespeichert. Das Ger&#228;t startet in K&#252;rze neu...</p>
  <script>setTimeout(function(){window.location='/';},5000);</script>
</div>
)rawhtml";

const char HTML_SAVED_LIVE[] PROGMEM = R"rawhtml(
<div class="card">
  <h1>&#10003; Gespeichert</h1>
  <p>Konfiguration gespeichert und sofort angewendet.</p>
  <a class="btn" href="/status" style="margin-top:12px">Zum Status</a>
</div>
)rawhtml";
