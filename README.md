## Biodigester Automation and Monitoring System

ESP32-based temperature control and real-time monitoring for a biodigester. The active firmware is `biogesterv2/biogesterv2.ino`.

## What it does

- Reads two DS18B20 temperature sensors (biodigester slurry + hot water tank)
- Controls a heater and circulation pump via SSR with hysteresis (ON below 37 C, OFF above 38 C)
- Displays status on a 20x4 I2C LCD
- Sends data over HTTPS to a Flask server hosted on Railway
- Web dashboard with live updates

## WiFi

Connects via **WPA2-Enterprise (EAP-TTLS)** for eduroam. Compile-time switchable to WPA-Personal by commenting out `#define USE_EDUROAM` in the biogesterv2.ino file.

## Server

Flask app in `server/`. Receives POST from ESP32, serves a live dashboard. Data is in-memory only.

## Hardware

- ESP32-WROOM-32E
- 2x DS18B20 waterproof probes (OneWire, GPIO4)
- 2x SSR driven via optocoupler modules (GPIO25, GPIO26)
- 20x4 LCD (I2C, PCF8574)

## Wiring

### Power

```
+5V Rail ───┬── ESP32 VIN
            ├── LCD VCC
            ├── Optocoupler Module 1 (DC+)
            ├── Optocoupler Module 2 (DC+)
            └── I2C Level Shifter (HV)

+3.3V (ESP32 3V3) ───┬── DS18B20 VCC (both sensors)
                     ├── 4.7kΩ pull-up → DATA line
                     └── I2C Level Shifter (LV)

GND ───┬── ESP32 GND
       ├── LCD GND
       ├── DS18B20 GND (both sensors)
       ├── Optocoupler Modules (GND + DC-)
       └── I2C Level Shifter (GND)
```

### DS18B20 OneWire Bus

Both sensors share a single OneWire bus on GPIO4. A 4.7k pull-up resistor connects from 3.3V to the DATA line. Each sensor has a unique 64-bit ROM address and is identified by index (0 = biodigester, 1 = water tank).

```
ESP32 3.3V ──── [4.7kΩ] ────┬── DS18B20 #1 DATA
                            └── DS18B20 #2 DATA
ESP32 GPIO4 ────────────────┘
```

---

Bakuho Goto - UCR-IDS, 2026
