## Biodigester Automation and Monitoring System

ESP32-based temperature control and real-time monitoring for a biodigester, developed at UCR-IDS (Universidad de Costa Rica - Instituto de Investigaciones en Desarrollo Sostenible).

## Project Structure

```
biogesterv2/        ESP32 firmware (Arduino sketch, active)
server/             Flask backend deployed on Railway
datasheets/         Component datasheets (ESP32, DS18B20, SSR, LCD, etc.)
articles/           Reference papers (manure properties, composting, dimensional analysis)
notes/              Design memos and sketches
```

## Firmware (`biogesterv2/biogesterv2.ino`)

The ESP32 firmware handles all sensing, control, display, and data upload in a single loop running every 5 seconds.

### Sensing and Control

- Reads two **DS18B20** temperature sensors via OneWire on GPIO4 (index 0 = biodigester slurry, index 1 = hot water tank)
- **Hysteresis control**: heater (SSR on GPIO25) and circulation motor (GPIO26) turn ON when biodigester temp drops below 37 C and OFF when it rises above 38 C
- On sensor disconnection, all outputs are shut off and an error is shown on the LCD

### Display

- 20x4 I2C LCD (PCF8574 at 0x27) shows biodigester temp, water temp, heater state, and motor state
- An I2C bus scan runs at startup for debugging

### Connectivity

- Connects to **eduroam** via WPA2-Enterprise (EAP-TTLS/PAP) by default
- Switchable to WPA-Personal at compile time by commenting out `#define USE_EDUROAM`
- Falls back to offline operation if WiFi fails within 30 seconds
- POSTs JSON (`biodigester_temp`, `water_temp`, `heater`, `motor`) to the Railway server over HTTPS

## Server (`server/app.py`)

Flask application deployed on Railway.

| Route | Method | Description |
|---|---|---|
| `/` | GET | Web dashboard (live readings, temperature chart, data table) |
| `/api/data` | POST | Receives JSON from ESP32, stores latest reading |
| `/api/data` | GET | Returns latest reading as JSON |
| `/api/history` | GET | Returns all historical readings as JSON |
| `/api/history/csv` | GET | Downloads historical readings as CSV |

- Latest reading kept in memory; historical data persisted to **PostgreSQL** (`DATABASE_URL` env var)
- Dashboard auto-refreshes every 3 seconds; marks device as disconnected if no data for 60 seconds

## Hardware

| Component | Details |
|---|---|
| MCU | ESP32-WROOM-32E |
| Temperature sensors | 2x DS18B20 waterproof probes (OneWire, GPIO4) |
| Heater control | SSR (40A) via optocoupler module (GPIO25) |
| Motor control | DC motor (G328 peristaltic pump) via optocoupler module (GPIO26) |
| Display | 20x4 I2C LCD (PCF8574 backpack) |

### Wiring

```
+5V Rail ───┬── ESP32 VIN
            ├── LCD VCC
            ├── Optocoupler Module 1 (DC+)
            └── Optocoupler Module 2 (DC+)

+3.3V (ESP32 3V3) ───┬── DS18B20 VCC (both sensors)
                     └── 4.7kΩ pull-up → DATA line

GND ────────┬── ESP32 GND
            ├── LCD GND
            ├── DS18B20 GND (both sensors)
            └── Optocoupler Modules (GND + DC-)
```

### DS18B20 OneWire Bus

Both sensors share a single OneWire bus on GPIO4 with a 4.7k pull-up to 3.3V. Sensors are identified by index (0 = biodigester, 1 = water tank).

```
ESP32 3.3V ──── [4.7kΩ] ────┬── DS18B20 #1 DATA
                            └── DS18B20 #2 DATA
ESP32 GPIO4 ────────────────┘
```

## Dependencies

### Firmware (Arduino / PlatformIO)

- WiFi, WiFiClientSecure, HTTPClient (ESP32 core)
- OneWire
- DallasTemperature
- LiquidCrystal_I2C
- Wire (ESP32 core)

### Server

See `server/requirements.txt`. Deployed via `server/Procfile` on Railway.

---

Bakuho Goto - UCR-IDS, 2026
