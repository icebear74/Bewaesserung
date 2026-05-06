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
  <h1>📍 Standort</h1>
  <form method="POST" action="/save_location" id="locForm">
    <label>Standort auf Karte auswählen (Marker verschieben)</label>
    <div id="map"></div>
    <div class="form-row">
      <div class="form-col">
        <label for="latitude">Breitengrad</label>
        <input type="number" id="latitude" name="latitude" value="{latitude}" step="0.0001" min="-90" max="90">
      </div>
      <div class="form-col">
        <label for="longitude">Längengrad</label>
        <input type="number" id="longitude" name="longitude" value="{longitude}" step="0.0001" min="-180" max="180">
      </div>
    </div>
    <label for="locationName">Ortsname (optional)</label>
    <input type="text" id="locationName" name="locationName" value="{locationName}" placeholder="z.B. Garten München" maxlength="63">
    <div style="margin-top:8px">
      <button class="btn" type="submit">💾 Speichern</button>
    </div>
  </form>
</div>
<script>
(function(){
  var lat = parseFloat(document.getElementById('latitude').value) || 48.1351;
  var lng = parseFloat(document.getElementById('longitude').value) || 11.5820;
  var map = L.map('map').setView([lat, lng], 13);
  L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png',{
    attribution:'© OpenStreetMap',maxZoom:19}).addTo(map);
  var marker = L.marker([lat,lng],{draggable:true}).addTo(map);
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
</script>
)rawhtml";

// ─── Hardware Config Page ─────────────────────────────────────────────────────

const char HTML_HARDWARE_PAGE[] PROGMEM = R"rawhtml(
<div class="card">
  <h1>⚙️ Hardware-Konfiguration</h1>
  <div class="alert-warning">
    ⚠️ Änderungen an der Relay-Konfiguration erfordern einen Neustart.
  </div>
  <form method="POST" action="/save_hardware">
    <label for="relayCount">Anzahl Relais (0-8)</label>
    <input type="number" id="relayCount" name="relayCount" value="{relayCount}" min="0" max="8" onchange="updatePins()">
    <div id="pinConfig">
      {relay_pins_html}
    </div>
    <label style="display:flex;align-items:center;margin-bottom:14px">
      <input type="checkbox" id="relayInverted" name="relayInverted" {relay_inverted_checked}>
      Relais aktiv LOW (invertiert, typisch für Relaismodule mit IN-Pin)
    </label>
    <div style="margin-top:8px">
      <button class="btn" type="submit">💾 Speichern &amp; Neu starten</button>
    </div>
  </form>
</div>
<script>
function updatePins(){
  var count = parseInt(document.getElementById('relayCount').value)||0;
  var div = document.getElementById('pinConfig');
  var html = '';
  for(var i=0;i<count;i++){
    var existing = div.querySelector('[name="pin'+i+'"]');
    var val = existing ? existing.value : '-1';
    html += '<label>Relais '+(i+1)+' GPIO-Pin</label>';
    html += '<input type="number" name="pin'+i+'" value="'+val+'" min="-1" max="39" placeholder="-1 = nicht belegt">';
  }
  div.innerHTML = html;
}
</script>
)rawhtml";

// ─── Watering Config Page (Phase 2 placeholder) ───────────────────────────────

const char HTML_WATERING_PAGE[] PROGMEM = R"rawhtml(
<div class="card">
  <h1>💧 Bewässerungsplan</h1>
  <div class="alert-info">
    ℹ️ Die vollständige Bewässerungskonfiguration wird in Phase 2 implementiert.
  </div>
  <p style="color:#666;margin-top:12px">
    Hier werden Sie Bewässerungspläne für jedes Relais einrichten können, 
    einschließlich Wochentage, Startzeit und Dauer.
  </p>
  {watering_status}
</div>
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
  <h1>404 – Seite nicht gefunden</h1>
  <p>Die angeforderte Seite existiert nicht.</p>
  <a class="btn" href="/" style="margin-top:12px">Zur Startseite</a>
</div>
)rawhtml";

// ─── Save Confirmation Page ───────────────────────────────────────────────────

const char HTML_SAVED_RESTART[] PROGMEM = R"rawhtml(
<div class="card">
  <h1>✅ Gespeichert</h1>
  <p>Konfiguration gespeichert. Das Gerät startet in Kürze neu...</p>
  <script>setTimeout(function(){window.location='/';},5000);</script>
</div>
)rawhtml";

const char HTML_SAVED_LIVE[] PROGMEM = R"rawhtml(
<div class="card">
  <h1>✅ Gespeichert</h1>
  <p>Konfiguration gespeichert und sofort angewendet.</p>
  <a class="btn" href="/status" style="margin-top:12px">Zum Status</a>
</div>
)rawhtml";
