## Biodigester Automation and Monitoring System

ESP32-based temperature control and real-time monitoring for a 40 L biodigester, developed at UCR-IDS (Universidad de Costa Rica - Instituto de Investigaciones en Desarrollo Sostenible).

The tank is heated directly by a silicone drum heater band wrapped around the tank and covered with insulation. The ESP32 switches the heater through an SSR based on the slurry temperature.

## Project Structure

```
biogesterv2/        ESP32 firmware (Arduino sketch, active)
server/             Flask backend deployed on Railway
datasheets/         Component datasheets (ESP32, DS18B20, SSR, LCD, etc.)
articles/           Reference papers (manure properties, composting, dimensional analysis)
```

## Firmware (`biogesterv2/biogesterv2.ino`)

The ESP32 firmware handles sensing, control, display, and data upload in a single loop running every 5 seconds.

### Sensing and Control

- Reads one **DS18B20** temperature sensor (biodigester slurry) via OneWire on GPIO4
- **Single-threshold control**: heater (SSR on GPIO25) turns ON when the slurry temperature is below 37.5 °C and OFF otherwise
- **Minimum ON/OFF time of 1 minute**: after each switch, the heater state is held for at least 60 s to avoid rapid cycling around the setpoint
- On sensor disconnection, the heater is turned OFF immediately (ignoring the minimum time) and an error is shown on the LCD

### Display

- 20x4 I2C LCD (PCF8574 at 0x27) shows biodigester temp and heater state
- An I2C bus scan runs at startup for debugging

### Connectivity

- Connects to **eduroam** via WPA2-Enterprise (EAP-TTLS/PAP) by default
- Switchable to WPA-Personal at compile time by commenting out `#define USE_EDUROAM`
- Credentials live in `biogesterv2/secrets.h`, which is git-ignored. Copy `secrets.h.example` to `secrets.h` and fill in your values
- Falls back to offline operation if WiFi fails within 30 seconds
- POSTs JSON (`biodigester_temp`, `heater`) to the Railway server over HTTPS

## Server (`server/app.py`)

Flask application deployed on Railway.

| Route | Method | Description |
|---|---|---|
| `/` | GET | Web dashboard (live readings, temperature chart, data table) |
| `/api/data` | POST | Receives JSON from ESP32, stores latest reading |
| `/api/data` | GET | Returns latest reading as JSON |
| `/api/history` | GET | Returns all historical readings as JSON |
| `/api/history` | DELETE | Deletes all historical readings |
| `/api/history/csv` | GET | Downloads historical readings as CSV |

- Latest reading kept in memory; historical data (time, temperature, heater state) persisted to **PostgreSQL** (`DATABASE_URL` env var) at most once per minute
- Dashboard auto-refreshes every 3 seconds; marks device as disconnected if no data for 60 seconds

## Hardware

| Component | Details |
|---|---|
| MCU | ESP32-WROOM-32E |
| Temperature sensor | 1x DS18B20 waterproof probe (OneWire, GPIO4) |
| Heater | NORJIN 5-gallon silicone drum heater band, 120 V / 800 W, 200 x 860 mm, built-in thermostat 30–150 °C |
| Heater control | SSR (40A, zero-cross) via optocoupler module (GPIO25) |
| Display | 20x4 I2C LCD (PCF8574 backpack) |
| AC power protection | Plug-in GFCI (enchufable) upstream of the extension cord feeding the SSR/heater |

### Wiring

```
+5V Rail ───┬── ESP32 VIN
            ├── LCD VCC
            └── Optocoupler Module (DC+)

+3.3V (ESP32 3V3) ───┬── DS18B20 VCC
                     └── 4.7kΩ pull-up → DATA line

GND ────────┬── ESP32 GND
            ├── LCD GND
            ├── DS18B20 GND
            └── Optocoupler Module (GND + DC-)

ESP32 GPIO4  ── DS18B20 DATA
ESP32 GPIO25 ── Optocoupler PWM → SSR (DC+/DC-)

AC 120V ── GFCI ── SSR ── Drum heater (built-in thermostat in series)
```

### Safety

- The drum heater's built-in thermostat is in series with the SSR and acts as a hardware temperature limit that works even if the firmware hangs or the SSR fails closed. Set the dial only slightly above the control setpoint (the heater surface runs hotter than the slurry), and keep it well below the softening temperature of the tank material
- The heater is fully covered, so no hot surface is exposed; the GFCI protects against ground faults

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

## Future Plans

### 1. Split into Safety and Monitoring ESP32s

Separate the controller into two boards so that the network-connected MCU never drives any power device:

- **ESP32-A (safety/control, offline)**: DS18B20, SSR, all control logic
- **ESP32-B (monitoring)**: WiFi/HTTPS to Railway, LCD
- A → B communication over one-way UART (A's RX left unconnected), so a compromised or hung B cannot affect the heater

### 2. Rugged Temperature Probe

Replace the current DS18B20 probe (very thin leads, easily broken) with a sturdier waterproof probe.

### 3. Similitude Analysis

Buckingham Pi similitude analysis against the full-scale digester may be revisited if needed.

---

Bakuho Goto - UCR-IDS, 2026
