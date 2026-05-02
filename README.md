## Biodigester Automation and Monitoring System

An ESP32-based automation subsystem for temperature control and real-time process monitoring of a biodigester.

---

## Objective

- Monitor the internal conditions of the biodigester in real time
- Maintain the biodigester temperature within the target mesophilic range
- Provide local and remote access to sensor data
- Support future control logic and safety functions

---

## Measured Variables

The ESP32 acquires the following process variables:

- **Biodigester slurry (boñiga) temperature** — inside the main tank
- **Hot water tank temperature** — water used in the external heating loop

Both temperatures are measured using DS18B20 waterproof probe sensors on a shared OneWire bus (GPIO4).

---

## Control Strategy

Hysteresis-based temperature control using the biodigester slurry temperature:

| Condition | Action |
|-----------|--------|
| Biodigester temp < 37.0 °C | Heater ON, Pump ON |
| Biodigester temp > 38.0 °C | Heater OFF, Pump OFF |

If either sensor reports a disconnection error, the heater and pump are immediately turned off as a safety measure.

---

## Hardware

| Component | Model / Spec | Quantity |
|-----------|-------------|----------|
| Microcontroller | ESP32-WROOM-32E | 1 |
| Temperature sensor | DS18B20 waterproof probe (SUS316) | 2 |
| Solid State Relay (SSR) | AC load switching | 2 |
| Optocoupler MOSFET Driver Module | 3.3V→5V level shift + optical isolation | 2 |
| LCD Display | 2004A 20x4 (5V, I2C via PCF8574) | 1 |
| I2C Level Shifter | BSS138-based bidirectional module | 1 |
| Pull-up Resistor | 4.7kΩ (OneWire bus) | 1 |
| Enclosure | IP65-rated ABS waterproof box | 1 |

---

## GPIO Pin Assignment

| Component | Signal | ESP32 GPIO |
|-----------|--------|------------|
| DS18B20 (x2) | DATA (OneWire) | GPIO4 |
| LCD 2004A (I2C) | SDA | GPIO21 (via level shifter) |
| LCD 2004A (I2C) | SCL | GPIO22 (via level shifter) |
| Optocoupler Module — Heater | PWM | GPIO25 |
| Optocoupler Module — Pump | PWM | GPIO26 |

Available GPIOs for future expansion: GPIO5, GPIO18, GPIO19, GPIO34

---

## Wiring

### DS18B20 Temperature Sensors (x2)

Both sensors share a single OneWire bus. One 4.7kΩ pull-up resistor is required between 3.3V and the DATA line.

```
ESP32 3.3V ──┬──────────────────── DS18B20 #1 Red (VCC)
             ├──────────────────── DS18B20 #2 Red (VCC)
             │
             └─── [4.7kΩ] ───┐
                              │
ESP32 GPIO4 ──────────────────┼── DS18B20 #1 White (DATA)
                              └── DS18B20 #2 White (DATA)

ESP32 GND ───┬──────────────────── DS18B20 #1 Black (GND)
             └──────────────────── DS18B20 #2 Black (GND)
```

The 4.7kΩ resistor connects from 3.3V to the DATA line. GPIO4 and the sensor DATA wires meet at the same node — the resistor only pulls the bus HIGH, it does not sit between GPIO4 and the sensors.

Each DS18B20 has a unique 64-bit ROM address. Run `scanDS18B20Addresses()` on first boot to discover addresses, then configure them in `biogester.cpp`.

### SSR Driving Circuit (Optocoupler MOSFET Driver Module x2)

Each SSR is driven through an optocoupler MOSFET driver module that provides 3.3V→5V level shifting and optical isolation.

```
ESP32 GPIO25 (heater) ──→ Module PWM ──→ Optocoupler ──→ MOSFET
ESP32 GPIO26 (pump)   ──→ Module PWM ──→ Optocoupler ──→ MOSFET
                                                            │
5V (DC+) ──────────────────────────────────────────────→ OUT+ ──→ SSR (+)
GND (DC-) ─────────────────────────────────────────────→ OUT- ──→ SSR (-)
```

Module terminal connections:

| Terminal | Connection |
|----------|------------|
| PWM | ESP32 GPIO25 (heater) or GPIO26 (pump) |
| GND | ESP32 GND |
| DC+ | +5V rail |
| DC- | GND rail |
| OUT+ | SSR input (+) |
| OUT- | SSR input (-) |

### I2C Level Shifter (LCD)

A bidirectional BSS138-based level shifter is placed between the ESP32 (3.3V) and the LCD I2C backpack (5V).

```
ESP32 side (3.3V)           Level Shifter           LCD side (5V)
─────────────────           ─────────────           ─────────────
GPIO21 (SDA)  ←──────────→  LV1 ── HV1  ←──────────→  PCF8574 SDA
GPIO22 (SCL)  ←──────────→  LV2 ── HV2  ←──────────→  PCF8574 SCL
3.3V          ──────────→   LV              HV  ←──────────  5V
GND           ──────────→   GND            GND  ←──────────  GND
```

> **Note:** The LCD VCC (power) connects directly to the +5V rail, **not** through the level shifter. The level shifter only handles the SDA/SCL signal lines.

### Power Distribution

```
                        +5V Rail
ESP32 VIN (5V) ───────────┬──── LCD VCC (power)
                          ├──── Optocoupler Module 1 (DC+)
                          ├──── Optocoupler Module 2 (DC+)
                          └──── Level Shifter (HV)

                        +3.3V
ESP32 3.3V ───────────────┬──── DS18B20 VCC (Red, both sensors)
                          └──── [4.7kΩ] ──→ DATA line (GPIO4)
                          └──── Level Shifter (LV)

                        GND Rail
ESP32 GND ────────────────┬──── LCD GND
                          ├──── DS18B20 GND (Black, both sensors)
                          ├──── Optocoupler Module 1 (GND + DC-)
                          ├──── Optocoupler Module 2 (GND + DC-)
                          └──── Level Shifter (GND)
```

---

## Currently Implemented (in `biogester.cpp`)

- DS18B20 temperature reading (2 sensors, address-based or index-based)
- Hysteresis control of heater and circulation pump via SSR
- Sensor disconnection safety shutoff
- Local web server with dashboard (`/`) and JSON API (`/data`)
- Serial monitor output

---

## Planned / Not Yet Implemented

- **LCD display** — hardware is documented above but LCD code is not yet in `biogester.cpp`
- **Wi-Fi credentials in NVS** — currently hardcoded (`YOUR_SSID`), to be migrated to ESP32 NVS (Preferences library)
- **HTTPS data upload** — planned upload to Railway (Flask + PostgreSQL) monitoring platform
- **API key authentication** — for secure communication with the server

---

## Monitoring Website (Planned)

The monitoring platform will receive data from the ESP32 and display process variables in real time, hosted on Railway with Flask and PostgreSQL.

Planned functions:
- Live display of biodigester and water tank temperatures
- Heater and pump status visualization
- Historical data storage and graphs
- Alarm / notification system

---

## LCD Display (Planned)

A 20x4 LCD (2004A, 5V Blue Screen) will show on-site status:

- Line 1: Biodigester slurry temperature
- Line 2: Hot water tank temperature
- Line 3: Heating system status (ON / OFF)
- Line 4: Pump status (ON / OFF)

---

## Enclosure and Environmental Protection

- All electronics housed in an **IP65-rated waterproof enclosure** (ABS)
- Cable entry points use **cable glands** for waterproof wire pass-through
- **SSRs mounted on aluminum heat sinks** on enclosure exterior with thermal compound
- LCD visible through an acrylic window
- DS18B20 sensors use waterproof stainless steel probes (SUS316)

---

## Future Improvements

- Maximum water temperature protection
- Remote setpoint adjustment from the website
- Alarm system for abnormal temperature values
- pH sensor integration for fermentation monitoring (GPIO5, 18, or 19 available)

---

## Reference

An initial version of the control logic was developed in Arduino-style code (see `proto.cpp`) using a MAX6675 sensor and MOSFET-based hysteresis control. This has been migrated and expanded to the ESP32 platform in `biogester.cpp`.

---

Edit by: Bakuho Goto
