# Bewaesserung
ESP32-Bewässerungssystem

## Phase 1 – Grundsystem

### Architektur

Modularer Aufbau mit getrennten `.h`/`.cpp`-Dateien:

```
src/
├── main.cpp                     # Einstiegspunkt
├── core/
│   ├── Application.h/.cpp       # Haupt-Orchestrierung & Boot-Sequenz
│   ├── ConfigManager.h/.cpp     # LittleFS JSON Konfiguration
│   └── StateManager.h/.cpp      # Systemzustand-Automat
├── net/
│   ├── WifiManager.h/.cpp       # Multi-AP, WPS, Setup-AP
│   └── TimeSync.h/.cpp          # NTP + DS3231-Sync, Zeitzonenverwaltung
├── hw/
│   ├── OledStatus.h/.cpp        # SSD1306 128×64 Statusanzeige
│   ├── Ds3231Manager.h/.cpp     # DS3231 RTC-Verwaltung
│   └── RelayManager.h/.cpp      # Relais-/Pumpensteuerg (sicherheitsgesperrt)
└── web/
    ├── WebServer.h/.cpp         # HTTP-Server + DNS Captive Portal
    ├── WebPages.h               # PROGMEM HTML-Templates
    ├── WebHandlers.h/.cpp       # HTTP-Handler (Status, WLAN, Zeit, HW, ...)
```

### Start-Ablauf

1. I²C initialisieren (Wire.begin)
2. OLED initialisieren
3. DS3231 erkennen → Systemzeit setzen (falls vorhanden)
4. Konfiguration aus LittleFS laden
5. Relais initialisieren (alle AUS – sichere Grundstellung)
6. WLAN-Verbindung versuchen:
   - Alle APs mit gleicher SSID scannen und nach Signalstärke sortieren
   - Jeden AP nacheinander probieren
   - Falls alle scheitern → WPS (60 Sek.)
   - Falls WPS scheitert → Setup-AP starten
7. Webserver starten (Normal- oder Portal-Modus)
8. NTP-Zeitsync (bei WLAN-Verbindung), DS3231 aktualisieren
9. Betriebsmodus bestimmen:
   - WLAN verbunden → `RUNNING`
   - Kein WLAN, aber Bewässerungskonfig vorhanden → `RUNNING_OFFLINE`
   - Keine Bewässerungskonfig → `SETUP_REQUIRED`

### Sicherheitsregeln

- **Kein WLAN ≠ kein Betrieb**: Bei vorhandener Bewässerungskonfiguration läuft das System lokal weiter.
- **Erststart ist gesperrt**: Ohne Bewässerungsplan darf keine Pumpe geschaltet werden.
- **DS3231-Warnung**: Fehlt der RTC, wird im Webinterface ein roter Warnbalken angezeigt.
- **Relais sicher gesperrt**: `activateRelay()` erfordert explizites `armed=true`.

### Konfigurationsdateien (LittleFS)

| Datei             | Inhalt                                        |
|-------------------|-----------------------------------------------|
| `/config.json`    | WLAN, Zeitzone, NTP-Server, Standort          |
| `/hardware.json`  | Relais-Anzahl, GPIO-Pins, Invertierung        |
| `/watering.json`  | Bewässerungspläne (leer = Pumpen gesperrt)    |

### WebGUI-Seiten

- `/status` – Systemstatus (DS3231-Warnung, WLAN, Zeit, Betriebszustand)
- `/config_wifi` – WLAN-Einstellungen
- `/config_time` – Zeitzone & NTP-Server
- `/config_location` – Standort mit Kartenansicht (Leaflet)
- `/config_hardware` – Relais-Konfiguration (Neustart erforderlich bei Änderung)
- `/config_watering` – Bewässerungsplan (Phase 2)

### Setup-AP-Modus

Bei fehlendem WLAN startet das Gerät als Access Point:
- **SSID**: `Bewaesserung-Setup`
- **IP**: `192.168.4.1`
- **DNS**: Captive Portal (alle Anfragen → `/`)
- Keine Passwortabfrage für einfachen Erstzugang

### Build (PlatformIO)

```bash
pio run                    # Kompilieren
pio run -t upload          # Flashen
pio run -t uploadfs        # LittleFS hochladen
pio device monitor         # Serieller Monitor
```

### Abhängigkeiten

- `olikraus/U8g2` – OLED-Treiber (SSD1306)
- `adafruit/RTClib` – DS3231 RTC
- `bblanchon/ArduinoJson` – JSON-Konfiguration
- `arduino-libraries/NTPClient` – NTP-Zeitabfrage
