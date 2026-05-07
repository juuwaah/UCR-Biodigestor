#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// ===== Wi-Fi =====
// TODO: Migrate to NVS (Preferences library) for production use
const char* ssid     = "b(second)";
const char* password = "yoyoyoyo";

// ===== DS18B20 Pin (Biodigester + Water Tank) =====
// Both sensors share a single OneWire bus on GPIO4
// Red=3.3V, Black=GND, White=DATA
// 4.7kohm pull-up resistor required between VCC (3.3V) and DATA
#define ONE_WIRE_BUS 4

// ===== SSR Control Pin (Heater) =====
// Driven via Optocoupler MOSFET Driver Module (3.3V -> 5V level shift + optical isolation)
// ESP32 GPIO -> Module PWM -> Module OUT -> SSR DC input (3-32V)
#define SSR_HEATER 25

// ===== DC Motor Control Pin =====
// Single DC motor (G328) driven via Optocoupler MOSFET Driver Module
// ESP32 GPIO -> Module PWM -> Module OUT -> Motor (+/-)
// Flyback diode (1N4007) required across motor terminals
#define MOTOR_PIN 26

// ===== LCD (I2C, direct connection without level shifter) =====
// 2004A 20x4 LCD with PCF8574 backpack, powered at 5V
// SDA=GPIO21, SCL=GPIO22 (ESP32 default I2C)
// I2C pullups to 3.3V — remove/cut existing 5V pullups on PCF8574 board
#define LCD_ADDR 0x27
LiquidCrystal_I2C lcd(LCD_ADDR, 20, 4);

// ===== Railway Server =====
// TODO: Replace with your Railway deployment URL
const char* serverURL = "https://ucr-biogestor-production.up.railway.app/api/data";

// ===== Temperature Thresholds (Hysteresis Control) =====
const float TEMP_ON  = 37.0;  // Heater + Motor ON below this
const float TEMP_OFF = 38.0;  // Heater + Motor OFF above this

// ===== Objects =====
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature ds18b20(&oneWire);
WebServer server(80);

// ===== DS18B20 Sensor Addresses =====
// Run scanDS18B20Addresses() once to discover addresses, then set them here.
DeviceAddress bioSensorAddr   = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
DeviceAddress waterSensorAddr = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
bool addressesConfigured = false;

// ===== State Variables =====
bool heaterState = false;
bool motorState  = false;
float bioTemp    = 0.0;
float waterTemp  = 0.0;

// ===== Scan and print DS18B20 addresses =====
void scanDS18B20Addresses() {
  int deviceCount = ds18b20.getDeviceCount();
  Serial.printf("Found %d DS18B20 device(s) on OneWire bus\n", deviceCount);

  DeviceAddress addr;
  for (int i = 0; i < deviceCount; i++) {
    if (ds18b20.getAddress(addr, i)) {
      Serial.printf("  Sensor %d address: {", i);
      for (int j = 0; j < 8; j++) {
        Serial.printf("0x%02X", addr[j]);
        if (j < 7) Serial.print(", ");
      }
      Serial.println("}");
    }
  }

  if (deviceCount < 2) {
    Serial.printf("WARNING: Expected 2 DS18B20 sensors, found %d.\n", deviceCount);
  }

  bool allZero = true;
  for (int i = 0; i < 8; i++) {
    if (bioSensorAddr[i] != 0x00) { allZero = false; break; }
  }
  if (allZero) {
    Serial.println("WARNING: DS18B20 addresses not configured. Using index-based reading.");
    Serial.println("         Copy the addresses above into bioSensorAddr and waterSensorAddr.");
    addressesConfigured = false;
  } else {
    addressesConfigured = true;
    Serial.println("DS18B20 addresses configured. Using address-based reading.");
  }
}

// ===== LCD Update =====
void updateLCD() {
  lcd.setCursor(0, 0);
  lcd.printf("Bio:   %5.1f C", bioTemp);
  lcd.setCursor(0, 1);
  lcd.printf("Water: %5.1f C", waterTemp);
  lcd.setCursor(0, 2);
  lcd.printf("Heater: %s", heaterState ? "ON " : "OFF");
  lcd.setCursor(0, 3);
  lcd.printf("Motor:  %s", motorState ? "ON " : "OFF");
}

// ===== Web Endpoint: JSON =====
void handleData() {
  String json = "{";
  json += "\"biodigester_temp\":" + String(bioTemp, 1) + ",";
  json += "\"water_temp\":" + String(waterTemp, 1) + ",";
  json += "\"heater\":" + String(heaterState ? "true" : "false") + ",";
  json += "\"motor\":" + String(motorState ? "true" : "false");
  json += "}";
  server.send(200, "application/json", json);
}

// ===== Web Endpoint: Dashboard =====
void handleRoot() {
  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
  html += "<meta http-equiv='refresh' content='5'>";
  html += "<title>Biodigester Monitor</title>";
  html += "<style>body{font-family:sans-serif;max-width:600px;margin:40px auto;padding:0 20px}";
  html += ".card{background:#f5f5f5;border-radius:8px;padding:16px;margin:12px 0}";
  html += ".on{color:#e53e3e}.off{color:#38a169}</style></head><body>";
  html += "<h1>Biodigester Monitor v2</h1>";
  html += "<div class='card'><b>Biodigester Temp:</b> " + String(bioTemp, 1) + " &deg;C</div>";
  html += "<div class='card'><b>Water Tank Temp:</b> " + String(waterTemp, 1) + " &deg;C</div>";
  html += "<div class='card'><b>Heater:</b> <span class='" + String(heaterState ? "on'>ON" : "off'>OFF") + "</span></div>";
  html += "<div class='card'><b>Motor:</b> <span class='" + String(motorState ? "on'>ON" : "off'>OFF") + "</span></div>";
  html += "</body></html>";
  server.send(200, "text/html", html);
}

void setup() {
  Serial.begin(115200);

  // Pin initialization (OFF at startup)
  pinMode(SSR_HEATER, OUTPUT);
  pinMode(MOTOR_PIN, OUTPUT);
  digitalWrite(SSR_HEATER, LOW);
  digitalWrite(MOTOR_PIN, LOW);

  // DS18B20 initialization
  ds18b20.begin();
  delay(500);
  scanDS18B20Addresses();

  // I2C scan (debug: check if LCD is detected)
  Wire.begin();
  Serial.println("I2C scan...");
  for (byte addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.printf("  Found device at 0x%02X\n", addr);
    }
  }

  // LCD initialization (direct I2C, no level shifter)
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Biodigester v2");
  lcd.setCursor(0, 1);
  lcd.print("Starting...");

  // Wi-Fi connection
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  lcd.setCursor(0, 2);
  lcd.print("WiFi connecting...");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.println("Connected! IP: " + WiFi.localIP().toString());

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("WiFi OK");
  lcd.setCursor(0, 1);
  lcd.print(WiFi.localIP().toString());
  delay(2000);
  lcd.clear();

  // Web server
  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.begin();

  Serial.println("=== Biodigester v2 Control System Start ===");
}

void loop() {
  server.handleClient();

  // DS18B20 reading (both sensors)
  ds18b20.requestTemperatures();
  if (addressesConfigured) {
    bioTemp   = ds18b20.getTempC(bioSensorAddr);
    waterTemp = ds18b20.getTempC(waterSensorAddr);
  } else {
    bioTemp   = ds18b20.getTempCByIndex(0);
    waterTemp = ds18b20.getTempCByIndex(1);
  }

  // Sensor error check (only bioTemp required, waterTemp optional)
  Serial.printf("Raw readings - Bio: %.1f | Water: %.1f\n", bioTemp, waterTemp);
  if (bioTemp == DEVICE_DISCONNECTED_C) {
    Serial.println("Bio sensor error! Heater & Motor OFF.");
    digitalWrite(SSR_HEATER, LOW);
    digitalWrite(MOTOR_PIN, LOW);
    heaterState = false;
    motorState = false;
    lcd.setCursor(0, 3);
    lcd.print("SENSOR ERROR!       ");
    delay(2000);
    return;
  }

  // Hysteresis control (based on biodigester temperature)
  if (!heaterState && bioTemp < TEMP_ON) {
    heaterState = true;
    motorState = true;
    digitalWrite(SSR_HEATER, HIGH);
    digitalWrite(MOTOR_PIN, HIGH);
  }
  else if (heaterState && bioTemp > TEMP_OFF) {
    heaterState = false;
    motorState = false;
    digitalWrite(SSR_HEATER, LOW);
    digitalWrite(MOTOR_PIN, LOW);
  }

  // Update LCD
  updateLCD();

  // Send data to Railway server
  if (WiFi.status() == WL_CONNECTED) {
    WiFiClientSecure client;
    client.setInsecure();  // skip certificate verification
    HTTPClient http;
    http.begin(client, serverURL);
    http.addHeader("Content-Type", "application/json");
    String json = "{";
    json += "\"biodigester_temp\":" + String(bioTemp, 1) + ",";
    json += "\"water_temp\":" + String(waterTemp, 1) + ",";
    json += "\"heater\":" + String(heaterState ? "true" : "false") + ",";
    json += "\"motor\":" + String(motorState ? "true" : "false");
    json += "}";
    int code = http.POST(json);
    if (code > 0) {
      Serial.printf("POST -> %d\n", code);
    } else {
      Serial.printf("POST failed: %s\n", http.errorToString(code).c_str());
    }
    http.end();
  }

  // Serial output
  Serial.printf("Bio: %.1fC | Water: %.1fC | Heater: %s | Motor: %s\n",
    bioTemp, waterTemp,
    heaterState ? "ON" : "OFF",
    motorState ? "ON" : "OFF");

  delay(5000);
}
