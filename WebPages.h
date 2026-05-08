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
.container{max-width:1180px;margin:24px auto;padding:0 16px}
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
.table-wrap{width:100%;overflow-x:auto}
.compact-table th,.compact-table td{padding:8px 10px;vertical-align:top}
.status-layout{display:grid;grid-template-columns:minmax(280px,360px) minmax(0,1fr);gap:16px;align-items:start}
.status-panel{background:#fcfcfc;border:1px solid #e8ece8;border-radius:8px;padding:16px}
.config-section{margin-top:18px;padding-top:18px;border-top:1px solid #eef2ee}
.section-toolbar{display:flex;gap:8px;flex-wrap:wrap;align-items:center;margin-bottom:10px}
.hint-text{color:#667;font-size:13px;line-height:1.5}
@media (max-width:900px){.status-layout{grid-template-columns:1fr}.card{padding:18px}.container{padding:0 12px}}
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
  <a href="/watering_test">Testlauf</a>
  <a href="/runlog">Protokoll</a>
  <a href="/backup">Backup</a>
  <a href="/fs">Dateien</a>
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
//               {pump_status_html} {weather_html}

const char HTML_STATUS_PAGE[] PROGMEM = R"rawhtml(
<div class="card">
  <h1>&#128202; Systemstatus</h1>
  {ds3231_warning}
  {watering_locked_warning}
  <div class="status-layout">
    <div class="status-panel">
      <h2 style="margin-bottom:8px">&#128421;&#65039; System</h2>
      <table>
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
    <div class="status-panel">
      {pump_status_html}
    </div>
  </div>
  {weather_html}
  <p style="margin-top:14px">
    <a class="btn" href="/config_watering" style="font-size:13px;padding:7px 16px">&#128167; Bew&auml;sserung konfigurieren</a>
    <a class="btn" href="/config_hardware" style="font-size:13px;padding:7px 16px;margin-left:6px">&#9881;&#65039; Hardware</a>
  </p>
  <script>setTimeout(function(){window.location.reload();},30000);</script>
</div>
)rawhtml";

// ─── WiFi Config Page ─────────────────────────────────────────────────────────
// Tokens {password_field} {ota_password_field}: rendered server-side – plain
// text on first setup, masked on subsequent visits.

const char HTML_WIFI_PAGE[] PROGMEM = R"rawhtml(
<div class="card">
  <h1>&#128246; WLAN-Konfiguration</h1>
  <div class="alert-info">
    &#8505;&#65039; Nach dem Speichern der WLAN-Einstellungen startet das Ger&#228;t neu und verbindet sich mit dem neuen Netzwerk.<br>
    Das OTA-Passwort sch&#252;tzt drahtlose Firmware-Updates &uuml;ber das lokale WLAN.
  </div>
  <form method="POST" action="/save_wifi">
    <label for="ssid">WLAN-Name (SSID)</label>
    <input type="text" id="ssid" name="ssid" value="{ssid}" placeholder="Ihr WLAN-Name" maxlength="63">
    <label for="password">Passwort</label>
    {password_field}
    <label for="otaPassword">OTA-Passwort <span title="Dieses Passwort wird f&#252;r drahtlose Firmware-Updates per WLAN verwendet. Leer lassen = bisheriges Passwort behalten.">ⓘ</span></label>
    {ota_password_field}
    <label for="hostname">Ger&#228;tename (Hostname)</label>
    <input type="text" id="hostname" name="hostname" value="{hostname}" placeholder="Bewaesserung" maxlength="31">
    <div style="margin-top:8px">
      <button class="btn" type="submit">&#128190; Speichern &amp; Neu starten</button>
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
    document.getElementById('latitude').value = pos.lat.toFixed(4);
    document.getElementById('longitude').value = pos.lng.toFixed(4);
  });
  map.on('click',function(e){
    marker.setLatLng(e.latlng);
    document.getElementById('latitude').value = e.latlng.lat.toFixed(4);
    document.getElementById('longitude').value = e.latlng.lng.toFixed(4);
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
      document.getElementById('latitude').value = lat.toFixed(4);
      document.getElementById('longitude').value = lon.toFixed(4);
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
    <input type="hidden" id="pumpCount" name="pumpCount" value="{pumpCount}">
    <div id="pumpRows">{pump_rows_html}</div>
    <div id="noPumpsMsg" style="display:{noPumpsMsg};color:#999;font-style:italic;margin-bottom:8px">
      Noch keine Pumpe angelegt. Pumpe hinzuf&#252;gen &#8594;
    </div>
    <button type="button" class="btn" onclick="addPump()"
            style="margin-bottom:14px;background:#17a2b8">+ Pumpe hinzuf&#252;gen</button>

    <div style="margin-top:14px">
      <button class="btn" type="submit">&#128190; Speichern</button>
    </div>
  </form>
</div>
<script>
var _exp={expanders_json};
var _nextPumpIdx={pumpCount};
function _expLabel(e){return e.name+' ('+e.addr+', '+(e.chipType===1?'PCF8575':'PCF8574')+')';}
function _expOptions(selIdx){
  if(!_exp.length) return '<option value="0" disabled>&#x26A0; Kein Expander &#x2013; zuerst oben anlegen</option>';
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
  return '<div class="pump-entry" id="prow'+i+'" style="border:1px solid #ddd;padding:10px;margin-bottom:10px;border-radius:4px">'
  +'<div style="display:flex;justify-content:space-between;align-items:center;margin-bottom:6px">'
  +'<b>Pumpe #'+(i+1)+'</b>'
  +'<button type="button" onclick="deletePump('+i+')" '
  +'style="padding:3px 10px;background:#dc3545;color:#fff;border:none;border-radius:4px;cursor:pointer">'
  +'&#10005; L&#246;schen</button></div>'
  +'<div class="form-row">'
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
function addPump(){
  var i=_nextPumpIdx++;
  document.getElementById('pumpRows').insertAdjacentHTML('beforeend',mkRow(i,{pin:-1,maxRuntimeSec:300,enabled:true}));
  document.getElementById('pumpCount').value=_nextPumpIdx;
  document.getElementById('noPumpsMsg').style.display='none';
}
function deletePump(i){
  var el=document.getElementById('prow'+i);
  if(el)el.remove();
  // Recalculate pumpCount from remaining visible pump rows; unused indices just become empty on next save
  var rows=document.querySelectorAll('[id^="prow"]');
  document.getElementById('pumpCount').value=_nextPumpIdx; // server scans for present fields
  if(rows.length===0)document.getElementById('noPumpsMsg').style.display='block';
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
// Tokens: {watering_status} {pumpCount} {pump_names_json} {slotCount} {slot_names_json}
//         {weatherTemplateCount} {weather_template_names_json}
//         {slot_rows_html} {weather_template_rows_html} {assignCount}
//         {assignment_rows_html} {pump_assignment_overview_html}

const char HTML_WATERING_PAGE[] PROGMEM = R"rawhtml(
<div class="card">
  <h1>&#128167; Bew&#228;sserungsplan</h1>
  {watering_status}
  <div class="alert-info">
    &#128161; <b>1) Slots</b> definieren nur Zeit und Wiederholung.<br>
    <b>2) Wetter-Templates</b> enthalten mehrere Wetterregeln pro Vorlage.<br>
    <b>3) Pumpen-Zuweisungen</b> verbinden Slot + Wetter-Template (oder kein Template) + Pumpe.<br>
    <span title="Wichtig für kombinierte Regeln: Erst werden Aussetzen-Regeln geprüft. Nur wenn keine davon greift, werden Verkürzen/Verlängern-Regeln relativ zur Basislaufzeit addiert.">ⓘ Reihenfolge der Wetterlogik</span>: zuerst Aussetzen, danach Laufzeit anpassen.
  </div>
  <form method="POST" action="/save_watering" id="wf" onsubmit="prepareSubmit()">
    <input type="hidden" id="slotCount" name="slotCount" value="{slotCount}">
    <input type="hidden" id="weatherTemplateCount" name="weatherTemplateCount" value="{weatherTemplateCount}">
    <input type="hidden" id="assignCount" name="assignCount" value="{assignCount}">

    <div class="config-section" style="margin-top:8px;padding-top:0;border-top:none">
      <h2 style="color:#1a6b3c">1) Slots <span title="Ein Slot beschreibt nur wann grundsätzlich geprüft wird: Uhrzeit, Sonnenaufgang/Sonnenuntergang, Offset und Wiederholung. Wetter gehört nicht hier hinein.">ⓘ</span></h2>
      <div class="hint-text" style="margin-bottom:10px">Slots beschreiben nur den Zeitpunkt und die Wiederholung. Die Wetterlogik wird erst in der Zuweisung über ein Wetter-Template angewendet.</div>
      <div id="slots">{slot_rows_html}</div>
      <div id="noSlotsMsg" style="display:{noSlotsMsg};color:#999;font-style:italic;margin-bottom:8px">
        Noch kein Slot angelegt. Slot hinzuf&#252;gen &#8594;
      </div>
      <div class="section-toolbar">
        <button type="button" class="btn" onclick="addSlot()" style="background:#17a2b8">+ Slot hinzuf&#252;gen</button>
      </div>
    </div>

    <div class="config-section">
      <h2 style="color:#1a6b3c">2) Wetter-Templates <span title="Ein Wetter-Template ist eine Sammlung wiederverwendbarer Regeln. Ein einziges Template kann also z. B. Hitze-Verlängerung und Regen-Verkürzung gleichzeitig enthalten.">ⓘ</span></h2>
      <div class="hint-text" style="margin-bottom:10px">Templates k&ouml;nnen mehreren Pumpen-Zuweisungen zugeordnet werden. Skip-Regeln setzen komplett aus, Anpassungsregeln verk&uuml;rzen oder verl&auml;ngern die Basislaufzeit.</div>
      <div id="weatherTemplates">{weather_template_rows_html}</div>
      <div id="noWeatherTemplatesMsg" style="display:{noWeatherTemplatesMsg};color:#999;font-style:italic;margin-bottom:8px">
        Noch kein Wetter-Template angelegt. Optional, aber f&uuml;r Wiederverwendung empfohlen.
      </div>
      <div class="section-toolbar">
        <button type="button" class="btn" onclick="addWeatherTemplate()" style="background:#17a2b8">+ Wetter-Template hinzuf&#252;gen</button>
      </div>
    </div>

    <div class="config-section">
      <h2 style="color:#1a6b3c">3) Pumpen-Zuweisungen <span title="Eine Zuweisung entscheidet, welche Pumpe bei welchem Slot laufen soll und welche Wetterlogik dafür gilt.">ⓘ</span></h2>
      <div class="hint-text" style="margin-bottom:10px">Hier wird verkn&uuml;pft: <b>Slot</b> + <b>Wetter-Template/kein Template</b> + <b>Pumpe</b>. So kann dieselbe Zeitsteuerung je Pumpe unterschiedlich auf Wetter reagieren.</div>
      <div id="assignRows">{assignment_rows_html}</div>
      <div id="noAssignmentsMsg" style="display:{noAssignmentsMsg};color:#999;font-style:italic;margin-bottom:8px">
        Noch keine Pumpenzuweisung vorhanden.
      </div>
      <div class="section-toolbar">
        <button type="button" class="btn" onclick="addAssignment()" style="background:#17a2b8">+ Zuweisung hinzuf&#252;gen</button>
      </div>
    </div>

    <div class="config-section">
      <h2 style="color:#1a6b3c">4) Pumpen &#8594; &#220;bersicht</h2>
      <div id="pumpSlotOverview">{pump_assignment_overview_html}</div>
    </div>

    <div style="margin-top:12px;display:flex;gap:8px;flex-wrap:wrap">
      <button class="btn" type="submit">&#128190; Speichern</button>
      <a class="btn" href="/watering_test" style="background:#5a6268">&#128269; Testlauf</a>
    </div>
  </form>
</div>
<script>
var pumpCount={pumpCount};
var pumpNames={pump_names_json};
var slotNames={slot_names_json};
var weatherTemplateNames={weather_template_names_json};
var _nextSlotIdx={slotCount};
var _nextWeatherTemplateIdx={weatherTemplateCount};
var _nextAssignIdx={assignCount};
var dayL=['Mo','Di','Mi','Do','Fr','Sa','So'];
function pumpOpts(selIdx){
  if(pumpCount===0) return '<option value="0" disabled>&#x26A0; Keine Pumpen konfiguriert</option>';
  var s='';
  for(var i=0;i<pumpCount;i++) s+='<option value="'+i+'"'+(i===selIdx?' selected':'')+'>'+(pumpNames[i]||('Pumpe '+(i+1)))+'</option>';
  return s;
}
function slotOpts(selIdx){
  var s='',has=false;
  for(var i=0;i<slotNames.length;i++){
    if(slotNames[i]==null) continue;
    has=true;
    s+='<option value="'+i+'"'+(i===selIdx?' selected':'')+'>'+slotNames[i]+'</option>';
  }
  return has?s:'<option value="0" disabled>&#x26A0; Kein Slot vorhanden</option>';
}
function weatherTemplateOpts(selIdx){
  var s='<option value="-1"'+(selIdx<0?' selected':'')+'>Kein Template</option>';
  for(var i=0;i<weatherTemplateNames.length;i++){
    if(weatherTemplateNames[i]==null) continue;
    s+='<option value="'+i+'"'+(i===selIdx?' selected':'')+'>'+weatherTemplateNames[i]+'</option>';
  }
  return s;
}
function onTriggerChange(si,v){ var row=document.getElementById('offsetRow'+si); if(row) row.style.display=(v==='4')?'flex':'none'; }
function onRepeatModeChange(si,v){
  var days=document.getElementById('daysRow'+si), iv=document.getElementById('intervalRow'+si);
  var isIv=(v==='1');
  if(days) days.style.display=isIv?'none':'block';
  if(iv) iv.style.display=isIv?'flex':'none';
}
function toggleSlotEditor(si,forceOpen){ var body=document.getElementById('slotBody'+si); if(!body) return; if(forceOpen===true){body.style.display='block';return;} body.style.display=(body.style.display==='none'||body.style.display==='')?'block':'none'; }
function editSlot(si){ toggleSlotEditor(si,true); var inp=document.querySelector('input[name=\"s'+si+'_name\"]'); if(inp){inp.focus();inp.scrollIntoView({behavior:'smooth',block:'center'});} }
function refreshAssignmentSlotOptions(){
  document.querySelectorAll('#assignRows select[name$=\"_slot\"]').forEach(function(sel){
    var cur=parseInt(sel.value||'0'); sel.innerHTML=slotOpts(cur);
  });
}
function refreshAssignmentWeatherTemplateOptions(){
  document.querySelectorAll('#assignRows select[name$=\"_weatherTemplate\"]').forEach(function(sel){
    var cur=parseInt(sel.value||'-1'); sel.innerHTML=weatherTemplateOpts(cur);
  });
}
function updateSlotHeading(slotIdx,input){
  var b=input.closest('.pump-entry').querySelector('b');
  var n=(input.value||('Slot '+(slotIdx+1)));
  if(b) b.textContent='\u23F1 '+(slotIdx+1)+' \u2013 '+n;
  slotNames[slotIdx]=(slotIdx+1)+' - '+n;
  refreshAssignmentSlotOptions();
}
function updateWeatherTemplateHeading(idx,input){
  var wrap=input.closest('.pump-entry');
  var b=wrap?wrap.querySelector('b'):null;
  var n=(input.value||('Wetter '+(idx+1)));
  if(b) b.textContent='🌦️ '+n;
  weatherTemplateNames[idx]=n;
  refreshAssignmentWeatherTemplateOptions();
}
function addSlot(){
  var si=_nextSlotIdx++;
  slotNames[si]=(si+1)+' - Slot '+(si+1);
  document.getElementById('slots').insertAdjacentHTML('beforeend',mkSlot(si,{}));
  document.getElementById('slotCount').value=_nextSlotIdx;
  document.getElementById('noSlotsMsg').style.display='none';
  refreshAssignmentSlotOptions();
  editSlot(si);
}
function deleteSlot(si){
  var el=document.getElementById('slot'+si); if(el)el.remove();
  slotNames[si]=null;
  document.getElementById('slotCount').value=_nextSlotIdx;
  if(document.getElementById('slots').querySelectorAll('[id^=\"slot\"]').length===0)document.getElementById('noSlotsMsg').style.display='block';
  refreshAssignmentSlotOptions();
}
// Mapping zu ConfigManager.h:
// - WeatherRuleActionType: [0]=SKIP, [1]=REDUCE_RUNTIME, [2]=INCREASE_RUNTIME
// - WeatherRuleMetric: [0]=CURRENT_TEMP, [1]=FORECAST_TEMP_MAX, [2]=CURRENT_RAIN_MM,
//   [3]=CURRENT_RAIN_PROB, [4]=DAILY_RAIN_MM, [5]=DAILY_RAIN_PROB,
//   [6]=FORECAST_RAIN_SUM, [7]=FORECAST_RAIN_PROB_MAX
// - WeatherRuleComparison: [0]=>, [1]=>=, [2]=<, [3]=<=
// Muss in derselben Reihenfolge wie die C++-Enums in ConfigManager.h bleiben.
var weatherRuleActionLabels=['Aussetzen','Laufzeit verkürzen','Laufzeit verlängern'];
var weatherRuleMetricLabels=['Aktuelle Temperatur','Max. Temperatur in Zeitfenster','Aktueller Niederschlag (mm)','Aktuelle Regenwahrscheinlichkeit (%)','Regen heute (mm)','Regenwahrscheinlichkeit heute (%)','Regenmenge im Zeitfenster (mm)','Max. Regenwahrscheinlichkeit im Zeitfenster (%)'];
var weatherRuleOperatorLabels=['>','>=','<','<='];
var WEATHER_RULE_SKIP=0, WEATHER_RULE_REDUCE_RUNTIME=1, WEATHER_RULE_INCREASE_RUNTIME=2;
var WEATHER_METRIC_CURRENT_TEMP=0, WEATHER_METRIC_FORECAST_TEMP_MAX=1, WEATHER_METRIC_CURRENT_RAIN_MM=2, WEATHER_METRIC_CURRENT_RAIN_PROB=3, WEATHER_METRIC_DAILY_RAIN_MM=4, WEATHER_METRIC_DAILY_RAIN_PROB=5, WEATHER_METRIC_FORECAST_RAIN_SUM=6, WEATHER_METRIC_FORECAST_RAIN_PROB_MAX=7;
function metricUsesWindow(metric){ metric=parseInt(metric||0,10); return metric===WEATHER_METRIC_FORECAST_TEMP_MAX||metric===WEATHER_METRIC_FORECAST_RAIN_SUM||metric===WEATHER_METRIC_FORECAST_RAIN_PROB_MAX; }
function ruleSummary(d){
  d=d||{};
  var metric=parseInt(d.metric!=null?d.metric:WEATHER_METRIC_DAILY_RAIN_MM,10), cmp=parseInt(d.comparison!=null?d.comparison:1,10), action=parseInt(d.actionType!=null?d.actionType:WEATHER_RULE_SKIP,10);
  var unit=(metric===WEATHER_METRIC_CURRENT_TEMP||metric===WEATHER_METRIC_FORECAST_TEMP_MAX)?'°C':((metric===WEATHER_METRIC_CURRENT_RAIN_MM||metric===WEATHER_METRIC_DAILY_RAIN_MM||metric===WEATHER_METRIC_FORECAST_RAIN_SUM)?' mm':'%');
  var metricText=weatherRuleMetricLabels[metric]||'Wetterwert';
  if(metricUsesWindow(metric)) metricText+=' in den nächsten '+(d.windowHours||24)+'h';
  if(action===WEATHER_RULE_SKIP) return metricText+' '+(weatherRuleOperatorLabels[cmp]||'>=')+' '+(d.threshold!=null?d.threshold:0)+unit+' → Aussetzen';
  return metricText+' '+(weatherRuleOperatorLabels[cmp]||'>=')+' '+(d.threshold!=null?d.threshold:0)+unit+' → '+(action===WEATHER_RULE_REDUCE_RUNTIME?'-':'+')+(d.effectPercent||25)+'% Laufzeit';
}
function onRuleChanged(wi,ri){
  var row=document.getElementById('wtr'+wi+'_'+ri); if(!row) return;
  var sm=document.getElementById('wt'+wi+'r'+ri+'Summary'); if(!sm) return;
  sm.textContent=ruleSummary({
    actionType: parseInt((row.querySelector('select[name=\"wt'+wi+'_r'+ri+'_action\"]')||{}).value||WEATHER_RULE_SKIP,10),
    metric: parseInt((row.querySelector('select[name=\"wt'+wi+'_r'+ri+'_metric\"]')||{}).value||WEATHER_METRIC_DAILY_RAIN_MM,10),
    comparison: parseInt((row.querySelector('select[name=\"wt'+wi+'_r'+ri+'_operator\"]')||{}).value||1,10),
    threshold: (row.querySelector('input[name=\"wt'+wi+'_r'+ri+'_threshold\"]')||{}).value||0,
    windowHours: (row.querySelector('input[name=\"wt'+wi+'_r'+ri+'_windowHours\"]')||{}).value||24,
    effectPercent: (row.querySelector('input[name=\"wt'+wi+'_r'+ri+'_effectPct\"]')||{}).value||25
  });
}
function onRuleActionChange(wi,ri,v){
  var eff=document.getElementById('wt'+wi+'r'+ri+'EffectRow');
  if(eff) eff.style.display=(String(v)===String(WEATHER_RULE_SKIP))?'none':'flex';
  onRuleChanged(wi,ri);
}
function onRuleMetricChange(wi,ri,v){
  var wr=document.getElementById('wt'+wi+'r'+ri+'WindowRow');
  if(wr) wr.style.display=metricUsesWindow(v)?'flex':'none';
  onRuleChanged(wi,ri);
}
function mkWeatherRule(wi,ri,d){
  d=d||{};
  var action=d.actionType!=null?d.actionType:WEATHER_RULE_SKIP, metric=d.metric!=null?d.metric:WEATHER_METRIC_DAILY_RAIN_MM, cmp=d.comparison!=null?d.comparison:1;
  var threshold=d.threshold!=null?d.threshold:0, effect=d.effectPercent||25, win=d.windowHours||24, enabled=d.enabled!==false?'checked':'';
  var actOpts='', metOpts='', cmpOpts='';
  for(var a=0;a<weatherRuleActionLabels.length;a++) actOpts+='<option value=\"'+a+'\"'+(parseInt(action,10)===a?' selected':'')+'>'+weatherRuleActionLabels[a]+'</option>';
  for(var m=0;m<weatherRuleMetricLabels.length;m++) metOpts+='<option value=\"'+m+'\"'+(parseInt(metric,10)===m?' selected':'')+'>'+weatherRuleMetricLabels[m]+'</option>';
  for(var o=0;o<weatherRuleOperatorLabels.length;o++) cmpOpts+='<option value=\"'+o+'\"'+(parseInt(cmp,10)===o?' selected':'')+'>'+weatherRuleOperatorLabels[o]+'</option>';
  return '<div id=\"wtr'+wi+'_'+ri+'\" style=\"border:1px solid #dce8f8;border-radius:6px;padding:10px;margin:8px 0;background:#fff\">'
    +'<div style=\"display:flex;justify-content:space-between;align-items:center;gap:8px;flex-wrap:wrap;margin-bottom:8px\"><b>Regel '+(ri+1)+'</b><button type=\"button\" onclick=\"deleteWeatherRule('+wi+','+ri+')\" style=\"padding:3px 8px;background:#dc3545;color:#fff;border:none;border-radius:4px;cursor:pointer\">&#10005; Entfernen</button></div>'
    +'<div class=\"hint-text\" id=\"wt'+wi+'r'+ri+'Summary\" style=\"margin-bottom:8px\">'+ruleSummary({actionType:action,metric:metric,comparison:cmp,threshold:threshold,windowHours:win,effectPercent:effect})+'</div>'
    +'<div class=\"form-row\"><div class=\"form-col\"><label title=\"Aktive Regeln werden ausgewertet, deaktivierte Regeln bleiben gespeichert.\"><input type=\"checkbox\" name=\"wt'+wi+'_r'+ri+'_enabled\" '+enabled+' onchange=\"onRuleChanged('+wi+','+ri+')\"> Aktiv</label></div><div class=\"form-col\"><label title=\"Aussetzen stoppt die Pumpe komplett. Verkürzen/Verlängern ändern die Basislaufzeit prozentual.\">Regeltyp</label><select name=\"wt'+wi+'_r'+ri+'_action\" onchange=\"onRuleActionChange('+wi+','+ri+',this.value)\">'+actOpts+'</select></div></div>'
    +'<div class=\"form-row\"><div class=\"form-col\"><label title=\"Der Wetterwert wird mit dem Schwellwert verglichen. Für Zeitfenster-Regeln werden die nächsten Stunden betrachtet.\">Wetterwert</label><select name=\"wt'+wi+'_r'+ri+'_metric\" onchange=\"onRuleMetricChange('+wi+','+ri+',this.value)\">'+metOpts+'</select></div><div class=\"form-col\"><label title=\"Vergleicht Wetterwert und Schwellwert. Beispiel: > 30.\">Vergleich</label><select name=\"wt'+wi+'_r'+ri+'_operator\" onchange=\"onRuleChanged('+wi+','+ri+')\">'+cmpOpts+'</select></div><div class=\"form-col\"><label title=\"Ab diesem Wert greift die Regel.\">Schwellwert</label><input type=\"number\" name=\"wt'+wi+'_r'+ri+'_threshold\" value=\"'+threshold+'\" step=\"0.1\" oninput=\"onRuleChanged('+wi+','+ri+')\"></div></div>'
    +'<div id=\"wt'+wi+'r'+ri+'WindowRow\" class=\"form-row\" style=\"display:'+(metricUsesWindow(metric)?'flex':'none')+'\"><div class=\"form-col\"><label title=\"Nur für Zeitfenster-Regeln: wie viele nächste Stunden ausgewertet werden.\">Zeitfenster (h)</label><input type=\"number\" name=\"wt'+wi+'_r'+ri+'_windowHours\" value=\"'+win+'\" min=\"1\" max=\"48\" oninput=\"onRuleChanged('+wi+','+ri+')\"></div></div>'
    +'<div id=\"wt'+wi+'r'+ri+'EffectRow\" class=\"form-row\" style=\"display:'+(parseInt(action,10)===WEATHER_RULE_SKIP?'none':'flex')+'\"><div class=\"form-col\"><label title=\"Positive Prozentwerte beziehen sich immer auf die Basislaufzeit der Zuweisung.\">Effekt (%)</label><input type=\"number\" name=\"wt'+wi+'_r'+ri+'_effectPct\" value=\"'+effect+'\" min=\"1\" max=\"200\" oninput=\"onRuleChanged('+wi+','+ri+')\"></div></div></div>';
}
function addWeatherRule(wi,d){
  var wrap=document.getElementById('wt'+wi); if(!wrap) return;
  var next=parseInt(wrap.getAttribute('data-next-rule')||'0',10);
  wrap.setAttribute('data-next-rule', String(next+1));
  var list=document.getElementById('wtRules'+wi); if(list) list.insertAdjacentHTML('beforeend', mkWeatherRule(wi,next,d||{}));
  var cnt=document.getElementById('wt'+wi+'RuleCount'); if(cnt) cnt.value=String(next+1);
  var msg=document.getElementById('wt'+wi+'NoRulesMsg'); if(msg) msg.style.display='none';
}
function deleteWeatherRule(wi,ri){
  var el=document.getElementById('wtr'+wi+'_'+ri); if(el) el.remove();
  var list=document.getElementById('wtRules'+wi), msg=document.getElementById('wt'+wi+'NoRulesMsg');
  if(list&&msg&&list.children.length===0) msg.style.display='block';
}
function mkWeatherTemplate(wi,d){
  d=d||{}; var nm=d.name||('Wetter '+(wi+1)); var rules=d.rules||[];
  var html='<div class=\"pump-entry\" id=\"wt'+wi+'\" data-next-rule=\"'+rules.length+'\" style=\"border:1px solid #cfe0f6;padding:12px;margin-bottom:12px;border-radius:6px;background:#f7fbff\">'
    +'<div style=\"display:flex;justify-content:space-between;align-items:center;gap:8px;flex-wrap:wrap;margin-bottom:10px\"><b style=\"font-size:1.05em\">🌦️ '+nm+'</b><div style=\"display:flex;gap:6px;flex-wrap:wrap\"><button type=\"button\" onclick=\"editWeatherTemplate('+wi+')\" style=\"padding:3px 10px;background:#17a2b8;color:#fff;border:none;border-radius:4px;cursor:pointer\">&#9998; Bearbeiten</button><button type=\"button\" onclick=\"deleteWeatherTemplate('+wi+')\" style=\"padding:3px 10px;background:#dc3545;color:#fff;border:none;border-radius:4px;cursor:pointer\">&#10005; L&#246;schen</button></div></div>'
    +'<div class=\"form-row\"><div class=\"form-col\"><label title=\"Ein Wetter-Template ist eine wiederverwendbare Sammlung aus mehreren Wetterregeln.\">Name</label><input type=\"text\" name=\"wt'+wi+'_name\" value=\"'+nm+'\" maxlength=\"31\" oninput=\"updateWeatherTemplateHeading('+wi+',this)\" required></div></div>'
    +'<div class=\"hint-text\" style=\"margin-bottom:8px\">Ein Template kann mehrere Regeln enthalten. Reihenfolge im System: erst <b>Aussetzen</b>, danach <b>Verkürzen/Verlängern</b>. Zuschläge und Abzüge beziehen sich immer auf die Basislaufzeit der Zuweisung.</div>'
    +'<input type=\"hidden\" name=\"wt'+wi+'_ruleCount\" id=\"wt'+wi+'RuleCount\" value=\"'+rules.length+'\">'
    +'<div id=\"wtRules'+wi+'\">';
  for(var i=0;i<rules.length;i++) html+=mkWeatherRule(wi,i,rules[i]);
  html+='</div><div id=\"wt'+wi+'NoRulesMsg\" class=\"hint-text\" style=\"display:'+(rules.length?'none':'block')+';margin:8px 0\">Noch keine Wetterregel definiert.</div>'
    +'<button type=\"button\" class=\"btn\" onclick=\"addWeatherRule('+wi+')\" style=\"background:#5c88c8;padding:7px 14px;font-size:13px\">+ Regel hinzuf&#252;gen</button></div>';
  return html;
}
function addWeatherTemplate(){
  var wi=_nextWeatherTemplateIdx++;
  weatherTemplateNames[wi]='Wetter '+(wi+1);
  document.getElementById('weatherTemplates').insertAdjacentHTML('beforeend',mkWeatherTemplate(wi,{}));
  document.getElementById('weatherTemplateCount').value=_nextWeatherTemplateIdx;
  document.getElementById('noWeatherTemplatesMsg').style.display='none';
  refreshAssignmentWeatherTemplateOptions();
  editWeatherTemplate(wi);
}
function deleteWeatherTemplate(wi){
  var el=document.getElementById('wt'+wi); if(el)el.remove();
  weatherTemplateNames[wi]=null;
  document.getElementById('weatherTemplateCount').value=_nextWeatherTemplateIdx;
  if(document.getElementById('weatherTemplates').querySelectorAll('[id^=\"wt\"]').length===0)document.getElementById('noWeatherTemplatesMsg').style.display='block';
  refreshAssignmentWeatherTemplateOptions();
}
function editWeatherTemplate(wi){ var inp=document.querySelector('input[name=\"wt'+wi+'_name\"]'); if(inp){inp.focus();inp.scrollIntoView({behavior:'smooth',block:'center'});} }
function addAssignment(){
  if(pumpCount===0){alert('Bitte zuerst Pumpen konfigurieren.');return;}
  if(document.getElementById('slots').querySelectorAll('[id^=\"slot\"]').length===0){alert('Bitte zuerst einen Slot anlegen.');return;}
  var ai=_nextAssignIdx++;
  var html='<div class=\"pump-entry\" style=\"border:1px solid #eadfb7;padding:12px;margin-bottom:12px;border-radius:6px;background:#fffdf5\" id=\"asrow'+ai+'\">'
    +'<div style=\"display:flex;justify-content:space-between;align-items:center;gap:8px;flex-wrap:wrap;margin-bottom:10px\"><b style=\"font-size:1.05em\">🔗 Zuweisung '+(ai+1)+'</b><div style=\"display:flex;gap:6px;flex-wrap:wrap\"><button type=\"button\" onclick=\"editAssignment('+ai+')\" style=\"padding:3px 8px;background:#17a2b8;color:#fff;border:none;border-radius:4px;cursor:pointer\">&#9998; Bearbeiten</button><button type=\"button\" onclick=\"deleteAssignment('+ai+')\" style=\"padding:3px 8px;background:#dc3545;color:#fff;border:none;border-radius:4px;cursor:pointer\">&#10005; L&#246;schen</button></div></div>'
    +'<div class=\"form-row\">'
    +'<div class=\"form-col\"><label title=\"Ein Slot beschreibt nur, wann geprüft wird. Er enthält keine pumpenspezifische Wetterlogik.\">Slot</label><select name=\"as'+ai+'_slot\">'+slotOpts(0)+'</select></div>'
    +'<div class=\"form-col\"><label title=\"Die konkrete Pumpe, die bei dieser Zuweisung laufen soll.\">Pumpe</label><select name=\"as'+ai+'_pump\">'+pumpOpts(0)+'</select></div>'
    +'<div class=\"form-col\"><label title=\"Hier wird festgelegt, welche Wetterregeln für genau diese Pumpe und diesen Slot gelten. Kein Template = reine Zeitsteuerung.\">Wetter-Template</label><select name=\"as'+ai+'_weatherTemplate\">'+weatherTemplateOpts(-1)+'</select></div>'
    +'<div class=\"form-col\"><label title=\"Basislaufzeit ohne Wetteranpassung. Zuschläge und Abzüge der Regeln beziehen sich auf diesen Wert.\">Dauer (s)</label><input type=\"number\" name=\"as'+ai+'_duration\" value=\"60\" min=\"1\" max=\"7200\"></div>'
    +'</div></div>';
  document.getElementById('assignRows').insertAdjacentHTML('beforeend',html);
  document.getElementById('assignCount').value=_nextAssignIdx;
  document.getElementById('noAssignmentsMsg').style.display='none';
}
function deleteAssignment(ai){
  var el=document.getElementById('asrow'+ai); if(el)el.remove();
  if(document.getElementById('assignRows').querySelectorAll('[id^=\"asrow\"]').length===0)document.getElementById('noAssignmentsMsg').style.display='block';
}
function editAssignment(ai){ var row=document.getElementById('asrow'+ai); if(!row)return; var sel=row.querySelector('select'); if(sel){sel.focus();row.scrollIntoView({behavior:'smooth',block:'center'});} }
function mkSlot(si,d){
  d=d||{}; var si1=si+1; var nm=d.name||('Slot '+si1); var en=d.enabled!==false?'checked':''; var tr=d.triggerType||0;
  var hr=d.fixedHour!=null?d.fixedHour:6, mn=d.fixedMinute!=null?d.fixedMinute:0;
  var timVal=(hr<10?'0':'')+hr+':'+(mn<10?'0':'')+mn;
  var offMin=d.offsetMinutes||0, offBase=d.offsetBase||0, days=d.days!=null?d.days:0x7F;
  var repeatMode=d.repeatMode!=null?d.repeatMode:0, intervalDays=d.intervalDays||1, intervalAnchor=d.intervalAnchor||'';
  var daysHtml=''; for(var dd=0;dd<7;dd++){var c=(days&(1<<dd))?'checked':''; daysHtml+='<label style=\"margin-right:7px\"><input type=\"checkbox\" name=\"s'+si+'_d'+dd+'\" '+c+'> '+dayL[dd]+'</label>';}
  var trNames=['Feste Uhrzeit','Sonnenaufgang','Sonnenuntergang','Mittagszeit','Offset (relativ zu Referenz)'], trOpts=''; for(var t=0;t<5;t++) trOpts+='<option value=\"'+t+'\"'+(tr===t?' selected':'')+'>'+trNames[t]+'</option>';
  var repNames=['Wochentage','Intervall (alle N Tage)'], repOpts=''; for(var r=0;r<2;r++) repOpts+='<option value=\"'+r+'\"'+(repeatMode===r?' selected':'')+'>'+repNames[r]+'</option>';
  var baseNames=['Sonnenaufgang','Sonnenuntergang','Mittagszeit'], baseOpts=''; for(var b=0;b<3;b++) baseOpts+='<option value=\"'+b+'\"'+(offBase===b?' selected':'')+'>'+baseNames[b]+'</option>';
  var offDisp=(tr===4)?'flex':'none';
  return '<div class=\"pump-entry\" id=\"slot'+si+'\" style=\"border:1px solid #b3d4b3;padding:12px;margin-bottom:12px;border-radius:6px;background:#f9fff9\">'
    +'<div style=\"display:flex;justify-content:space-between;align-items:center;margin-bottom:8px\"><b style=\"font-size:1.05em\">&#128337; '+si1+' &ndash; '+nm+'</b><div style=\"display:flex;gap:6px;flex-wrap:wrap\"><button type=\"button\" onclick=\"editSlot('+si+')\" style=\"padding:3px 10px;background:#17a2b8;color:#fff;border:none;border-radius:4px;cursor:pointer\">&#9998; Bearbeiten</button><button type=\"button\" onclick=\"deleteSlot('+si+')\" style=\"padding:3px 10px;background:#dc3545;color:#fff;border:none;border-radius:4px;cursor:pointer\">&#10005; L&#246;schen</button></div></div>'
    +'<div id=\"slotBody'+si+'\" style=\"display:block\"><div class=\"form-row\"><div class=\"form-col\"><label title=\"Nur aktive Slots werden geprüft.\"><input type=\"checkbox\" name=\"s'+si+'_enabled\" '+en+'> Aktiv</label></div><div class=\"form-col\"><label title=\"Ein Slot ist ein reiner Zeit-Auslöser. Wetterlogik gehört in die Zuweisung über ein Wetter-Template.\">Name</label><input type=\"text\" name=\"s'+si+'_name\" value=\"'+nm+'\" maxlength=\"31\" oninput=\"updateSlotHeading('+si+',this)\" required></div></div>'
    +'<div class=\"form-row\"><div class=\"form-col\"><label title=\"Legt fest, worauf sich der Slot zeitlich bezieht: feste Uhrzeit, Sonnenaufgang, Sonnenuntergang oder Offset relativ dazu.\">Ausl&ouml;ser</label><select name=\"s'+si+'_trigger\" onchange=\"onTriggerChange('+si+',this.value)\">'+trOpts+'</select></div><div class=\"form-col\"><label title=\"Diese Uhrzeit wird bei festen Slots direkt verwendet und dient bei astronomischen Triggern als Fallback, falls keine Wetter-/Astronomiedaten vorliegen.\">Uhrzeit / Fallback</label><input type=\"time\" name=\"s'+si+'_time\" value=\"'+timVal+'\"></div></div>'
    +'<div id=\"offsetRow'+si+'\" style=\"display:'+offDisp+'\" class=\"form-row\"><div class=\"form-col\"><label title=\"Relativ bedeutet: Der Start wird von Sonnenaufgang, Sonnenuntergang oder Mittagszeit aus berechnet.\">Offset-Basis</label><select name=\"s'+si+'_offsetBase\">'+baseOpts+'</select></div><div class=\"form-col\"><label title=\"Negativ = davor, positiv = danach. Beispiel: -30 bedeutet 30 Minuten vor der gewählten Basis.\">Offset (Min., negativ = davor, positiv = danach)</label><input type=\"number\" name=\"s'+si+'_offsetMin\" value=\"'+offMin+'\" min=\"-720\" max=\"720\"></div></div>'
    +'<div class=\"form-row\"><div class=\"form-col\"><label title=\"Wochentage = feste Tage. Intervall = alle N Tage ab dem Ankerdatum.\">Wiederholung</label><select name=\"s'+si+'_repeatMode\" onchange=\"onRepeatModeChange('+si+',this.value)\">'+repOpts+'</select></div></div>'
    +'<div id=\"daysRow'+si+'\" style=\"display:'+(repeatMode===1?'none':'block')+';margin-top:6px\">'+daysHtml+'</div>'
     +'<div id=\"intervalRow'+si+'\" style=\"display:'+(repeatMode===1?'flex':'none')+'\" class=\"form-row\"><div class=\"form-col\"><label title=\"Beispiel: 3 bedeutet alle drei Tage.\">Alle N Tage</label><input type=\"number\" name=\"s'+si+'_intervalDays\" value=\"'+intervalDays+'\" min=\"1\" max=\"90\"></div><div class=\"form-col\"><label title=\"Ab diesem Datum wird das Intervall gezählt.\">Startdatum (Anker)</label><input type=\"date\" name=\"s'+si+'_intervalAnchor\" value=\"'+intervalAnchor+'\"></div></div>'
     +'</div></div>';
}
function prepareSubmit(){ document.getElementById('slotCount').value=_nextSlotIdx; document.getElementById('weatherTemplateCount').value=_nextWeatherTemplateIdx; document.getElementById('assignCount').value=_nextAssignIdx; }
</script>
)rawhtml";

// ─── Watering Simulation/Test Page ────────────────────────────────────────────
// Token: {slot_options_json}

const char HTML_WATERING_TEST_PAGE[] PROGMEM = R"rawhtml(
<div class="card">
  <h1>&#129514; Bew&#228;sserung testen / simulieren</h1>
  <div class="alert-info">
    Nutzt exakt die gleiche Entscheidungs-Engine wie der Livebetrieb, aber ohne Hardware-Ausgabe.<br>
    Wetterregeln werden in derselben Reihenfolge wie live ausgewertet: erst Aussetzen, dann Laufzeit anpassen.
  </div>
  <div class="form-row">
    <div class="form-col"><label>Slot</label><select id="slotIndex"></select></div>
    <div class="form-col"><label>Simulierte Zeit</label><input type="datetime-local" id="simTime"></div>
  </div>
  <div class="form-row">
    <div class="form-col"><label>Wetterquelle</label>
      <select id="weatherState">
        <option value="fresh" selected>Simuliert (frisch)</option>
        <option value="stale">Simuliert (veraltet)</option>
        <option value="unavailable">Nicht verfügbar</option>
        <option value="live">Live-Cachedaten</option>
      </select>
    </div>
    <div class="form-col"><label>Temperatur (°C)</label><input type="number" id="temperature" value="20" step="0.1"></div>
  </div>
  <div class="form-row">
    <div class="form-col"><label>Regen heute (mm)</label><input type="number" id="dailyPrecipMm" value="0" step="0.1"></div>
    <div class="form-col"><label>Regenwahrscheinlichkeit heute (%)</label><input type="number" id="dailyPrecipPct" value="0" step="1" min="0" max="100"></div>
  </div>
  <div class="form-row">
    <div class="form-col"><label>Aktueller Niederschlag (mm)</label><input type="number" id="precipMm" value="0" step="0.1"></div>
    <div class="form-col"><label>Aktuelle Niederschlagswahrscheinlichkeit (%)</label><input type="number" id="precipProb" value="0" step="1" min="0" max="100"></div>
  </div>
  <div id="simWeatherContext" style="margin:10px 0;padding:10px;border:1px solid #dde7dd;border-radius:6px;background:#f8fff8"></div>
  <button class="btn" type="button" onclick="runSim()">Simulation starten</button>
  <div id="simResult" style="margin-top:14px"></div>
</div>
<script>
var slots={slot_options_json};
(function(){
  var s=document.getElementById('slotIndex');
  slots.forEach(function(it){ var o=document.createElement('option'); o.value=it.idx; o.textContent=it.name; s.appendChild(o); });
  var now=new Date(); now.setSeconds(0,0); document.getElementById('simTime').value=now.toISOString().slice(0,16);
  fetch('/api/weather').then(function(r){return r.json();}).then(renderWeatherContext).catch(function(){renderWeatherContext({available:false});});
})();
function esc(v){ return String(v==null?'':v).replace(/[&<>"']/g,function(c){return({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'})[c];}); }
function hhmmFromTs(ts){
  if(!ts) return '–';
  var d=new Date(ts*1000); return ('0'+d.getHours()).slice(-2)+':'+('0'+d.getMinutes()).slice(-2);
}
function actionLabelDe(a){
  return a==='skip'?'aussetzen':(a==='reduce'?'verkürzen':(a==='extend'?'verlängern':(a==='fallback'?'Fallback':'ausführen')));
}
function renderWeatherContext(d){
  var el=document.getElementById('simWeatherContext');
  if(!el) return;
  if(!d||!d.available){ el.innerHTML='<b>Wetterkontext:</b> keine Live-Daten verfügbar.'; return; }
  var h='<b>Wetterkontext (nächste 24h)</b><br>'
    +'Sonnenaufgang: '+hhmmFromTs(d.sunrise)
    +' | Sonnenuntergang: '+hhmmFromTs(d.sunset)
    +' | Daten '+(d.stale?'veraltet':'frisch');
  if(d.hourly24h&&d.hourly24h.length){
    h+='<table style=\"margin-top:8px\"><tr><th>Zeit</th><th>Temp</th><th>Regen</th><th>Wahrsch.</th></tr>';
    d.hourly24h.forEach(function(it){ h+='<tr><td>'+hhmmFromTs(it.ts)+'</td><td>'+esc(it.temp)+'°C</td><td>'+esc(it.precipMm)+' mm</td><td>'+esc(it.precipPct)+'%</td></tr>';});
    h+='</table>';
  }
  el.innerHTML=h;
}
function runSim(){
  var body='slotIndex='+encodeURIComponent(document.getElementById('slotIndex').value)
    +'&simTime='+encodeURIComponent(document.getElementById('simTime').value)
    +'&weatherState='+encodeURIComponent(document.getElementById('weatherState').value)
    +'&temperature='+encodeURIComponent(document.getElementById('temperature').value)
    +'&dailyPrecipMm='+encodeURIComponent(document.getElementById('dailyPrecipMm').value)
    +'&dailyPrecipPct='+encodeURIComponent(document.getElementById('dailyPrecipPct').value)
    +'&precipMm='+encodeURIComponent(document.getElementById('precipMm').value)
    +'&precipProb='+encodeURIComponent(document.getElementById('precipProb').value);
  var out=document.getElementById('simResult');
  out.innerHTML='...';
  fetch('/api/watering_simulate',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:body})
  .then(function(r){return r.json();})
  .then(function(d){
    var color=(d.action==='skip')?'#dc3545':(d.action==='reduce'?'#d48a00':(d.action==='extend'?'#0b7285':'#1a6b3c'));
    var h='<h2 style=\"margin-bottom:6px\">Ergebnis</h2>'
      +'<div style=\"padding:10px;border:1px solid #ddd;border-radius:6px;background:#fafafa\">'
      +'<p><b>Status:</b> <span style=\"color:'+color+'\">'+esc(actionLabelDe(d.action))+'</span></p>'
      +'<p><b>Grund:</b> '+esc(d.reason)+'</p>'
      +'<p><b>Trigger:</b> '+esc(d.triggerSource||'–')+'</p>'
      +'<p><b>Sonnenaufgang/Sonnenuntergang:</b> '+esc(d.sunrise||'–')+' / '+esc(d.sunset||'–')+'</p>'
      +'<p><b>Wetter:</b> '+esc(d.weatherJustification||'–')+'</p>'
      +'<p><b>Warnungen:</b> '+esc(d.warnings||'–')+'</p>'
      +'<p><b>Triggerzeit:</b> '+esc(d.triggerTime||'–')+' | <b>Tag passt:</b> '+(d.dayMatched?'ja':'nein')+' | <b>Minute passt:</b> '+(d.triggerMatched?'ja':'nein')+'</p>'
      +'<p><b>Gesamtdauer:</b> '+esc(d.totalDurationSec||0)+' s</p>';
    if(d.plan && d.plan.length){
      h+='<table><tr><th>#</th><th>Pumpe</th><th>Aktion</th><th>Laufzeit</th><th>Basis</th><th>Regeln</th><th>Grund</th></tr>';
      d.plan.forEach(function(p){ h+='<tr><td>'+esc(p.order)+'</td><td>'+esc(p.pumpName)+'</td><td>'+esc(actionLabelDe(p.action))+'</td><td>'+esc(p.durationSec)+' s</td><td>'+esc(p.baseDurationSec)+' s</td><td>'+esc(p.appliedRules||'–')+'</td><td>'+esc(p.reason||'')+' ('+esc(p.policySource||'')+')</td></tr>'; });
      h+='</table>';
    } else {
      h+='<p style=\"color:#999\">Keine Pumpen-Ausf&#252;hrung geplant.</p>';
    }
    out.innerHTML=h+'</div>';
  })
  .catch(function(){ out.innerHTML='<p style=\"color:#dc3545\">Simulation fehlgeschlagen.</p>'; });
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
  <a class="btn" href="{saved_back_url}" style="margin-top:12px">&#8592; Zur&#252;ck</a>
  <a class="btn" href="/status" style="margin-top:12px;margin-left:8px">Zum Status</a>
</div>
)rawhtml";

// ─── Run Log Page ─────────────────────────────────────────────────────────────

const char HTML_RUNLOG_PAGE[] PROGMEM = R"rawhtml(
<div class="card">
  <h1>&#128203; Bew&#228;sserungs-Protokoll</h1>
  <p class="hint-text" style="margin-bottom:12px">
    Zeigt die letzten Pumpen-Aktivierungen (neueste zuerst, max. 500 Eintr&#228;ge, rotierend).
  </p>
  <div style="display:flex;gap:8px;margin-bottom:12px">
    <button class="btn" onclick="loadLog()">&#8635; Aktualisieren</button>
    <button class="btn btn-danger" onclick="clearLog()">&#128465; Protokoll l&ouml;schen</button>
  </div>
  <div id="logMsg" style="color:#dc3545;font-size:13px;min-height:18px;margin-bottom:8px"></div>
  <div class="table-wrap">
    <table class="compact-table">
      <thead><tr><th>Datum / Uhrzeit</th><th>Slot</th><th>Pumpe</th><th>Dauer</th></tr></thead>
      <tbody id="logBody"><tr><td colspan="4" style="color:#999">Lade...</td></tr></tbody>
    </table>
  </div>
</div>
<script>
function esc(s){var d=document.createElement('div');d.appendChild(document.createTextNode(s||''));return d.innerHTML;}
function fmtTs(t){
  if(!t)return '–';
  var d=new Date(t*1000);
  var pad=function(n){return n<10?'0'+n:n;};
  return d.getFullYear()+'-'+pad(d.getMonth()+1)+'-'+pad(d.getDate())
    +' '+pad(d.getHours())+':'+pad(d.getMinutes())+':'+pad(d.getSeconds());
}
async function loadLog(){
  var tbody=document.getElementById('logBody');
  tbody.innerHTML='<tr><td colspan="4" style="color:#999">Lade...</td></tr>';
  try{
    var r=await fetch('/api/runlog');
    if(!r.ok){tbody.innerHTML='<tr><td colspan="4" style="color:#dc3545">Fehler '+r.status+'</td></tr>';return;}
    var arr=await r.json();
    if(!arr||arr.length===0){tbody.innerHTML='<tr><td colspan="4" style="color:#999">Keine Eintr&auml;ge.</td></tr>';return;}
    var html='';
    arr.forEach(function(e){
      html+='<tr>'
        +'<td>'+esc(fmtTs(e.t))+'</td>'
        +'<td>'+esc(e.sn||'–')+'</td>'
        +'<td>'+esc(e.pn||'–')+'</td>'
        +'<td>'+esc(e.dur!=null?e.dur+' s':'–')+'</td>'
        +'</tr>';
    });
    tbody.innerHTML=html;
  }catch(ex){tbody.innerHTML='<tr><td colspan="4" style="color:#dc3545">'+esc(ex.toString())+'</td></tr>';}
}
async function clearLog(){
  if(!confirm('Protokoll wirklich l\u00f6schen?'))return;
  var r=await fetch('/api/runlog/clear',{method:'POST'});
  if(r.ok){document.getElementById('logMsg').innerText='';loadLog();}
  else document.getElementById('logMsg').innerText='Fehler beim L\u00f6schen.';
}
loadLog();
</script>
)rawhtml";

// ─── Backup & Restore Page ────────────────────────────────────────────────────
// Token: {restore_msg}  – filled server-side with success / error message

const char HTML_BACKUP_PAGE[] PROGMEM = R"rawhtml(
<div class="card">
  <h1>&#128190; Backup &amp; Restore</h1>
  {restore_msg}
  <div class="config-section">
    <h2>&#8595; Backup herunterladen</h2>
    <p class="hint-text" style="margin-bottom:12px">
      L&#228;dt eine einzige JSON-Datei herunter, die alle Konfigurationen enth&#228;lt
      (WLAN, Zeit, Standort, Hardware, Bew&#228;sserungsplan).
    </p>
    <a class="btn" href="/api/backup">&#8595; Backup jetzt herunterladen</a>
  </div>
  <div class="config-section">
    <h2>&#8593; Konfiguration wiederherstellen</h2>
    <p class="hint-text" style="margin-bottom:12px">
      W&#228;hlen Sie eine zuvor heruntergeladene Backup-Datei. Nach dem Hochladen wird die
      Konfiguration wiederhergestellt und das Ger&#228;t neu gestartet.
    </p>
    <form action="/api/restore" method="POST" enctype="multipart/form-data">
      <input type="file" name="backup" accept=".json" required
             style="margin-bottom:10px;display:block">
      <button type="submit" class="btn">&#8593; Restore starten</button>
    </form>
  </div>
  <div class="config-section">
    <h2>&#128193; Dateiverwaltung</h2>
    <p class="hint-text" style="margin-bottom:12px">
      Direkter Zugriff auf das LittleFS-Dateisystem f&#252;r erweiterte Verwaltungsaufgaben.
    </p>
    <a class="btn" href="/fs">Dateiverwaltung &#246;ffnen</a>
  </div>
</div>
)rawhtml";
