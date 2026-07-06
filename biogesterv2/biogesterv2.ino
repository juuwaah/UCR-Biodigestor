#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include "esp_eap_client.h"  // ESP32 Arduino Core 3.x

#define USE_EDUROAM

// 既存WiFi設定
const char* ssid     = "b(second)";
const char* password = "yoyoyoyo";

// Eduroam設定

#ifdef USE_EDUROAM
#define EDUROAM_SSID       "eduroam"
#define EDUROAM_ANON_ID    "anonymous@ucr.ac.cr"    // 外部identity
#define EDUROAM_IDENTITY   "bakuho.goto@ucr.ac.cr"  // 内部identity
#define EDUROAM_PASSWORD   "AguaCate2001##"
#endif

const char* serverURL = "https://ucr-biodigestor-production.up.railway.app/api/data";

#define ONE_WIRE_BUS 4
#define SSR_HEATER   25
#define MOTOR_PIN    26
#define LCD_ADDR     0x27

const float TEMP_ON    = 37.5;
const float TEMP_OFF   = 37.5;  // dead band = 0, single threshold
const float WATER_ON   = 53.0;
const float WATER_OFF  = 65.0;
const float WATER_ABS_MAX = 85.0;              // 絶対上限
const unsigned long HEATER_MAX_MS = 3600000UL; // 連続稼働上限 60分

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature ds18b20(&oneWire);
LiquidCrystal_I2C lcd(LCD_ADDR, 20, 4);

bool heaterState = false;
bool motorState  = false;
bool sensorFault = false;
float bioTemp    = 0.0;
float waterTemp  = 0.0;
unsigned long heaterOnSince = 0;

void setHeater(bool on) {
  if (on && !heaterState) heaterOnSince = millis();
  heaterState = on;
  digitalWrite(SSR_HEATER, on ? HIGH : LOW);
}

void setMotor(bool on) {
  motorState = on;
  digitalWrite(MOTOR_PIN, on ? HIGH : LOW);
}

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

void sendToServer() {
  if (WiFi.status() != WL_CONNECTED) return;

  char json[128];
  snprintf(json, sizeof(json),
    "{\"biodigester_temp\":%.1f,\"water_temp\":%.1f,\"heater\":%s,\"motor\":%s}",
    bioTemp, waterTemp,
    heaterState ? "true" : "false",
    motorState  ? "true" : "false");

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.begin(client, serverURL);
  http.setConnectTimeout(3000);
  http.setTimeout(3000);
  http.addHeader("Content-Type", "application/json");
  int code = http.POST(json);
  if (code <= 0) {
    Serial.printf("POST failed: %s\n", http.errorToString(code).c_str());
  }
  http.end();
}

void setup() {
  Serial.begin(115200);

  pinMode(SSR_HEATER, OUTPUT);
  pinMode(MOTOR_PIN, OUTPUT);
  digitalWrite(SSR_HEATER, LOW);
  digitalWrite(MOTOR_PIN, LOW);

  ds18b20.begin();

  Wire.begin();
  Serial.println("I2C scan:");
  for (byte a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0)
      Serial.printf("  Found: 0x%02X\n", a);
  }
  lcd.init();
  lcd.backlight();
  lcd.print("Biodigester v2");
  lcd.setCursor(0, 1);
  lcd.print("WiFi...");

#ifdef USE_EDUROAM
  Serial.println("Connecting to eduroam (WPA2-Enterprise)...");
  WiFi.disconnect(true);
  delay(1000);
  WiFi.mode(WIFI_STA);
  delay(100);
  esp_eap_client_set_identity((uint8_t*)EDUROAM_ANON_ID, strlen(EDUROAM_ANON_ID));
  esp_eap_client_set_username((uint8_t*)EDUROAM_IDENTITY, strlen(EDUROAM_IDENTITY));
  esp_eap_client_set_password((uint8_t*)EDUROAM_PASSWORD, strlen(EDUROAM_PASSWORD));
  esp_eap_client_set_ttls_phase2_method(ESP_EAP_TTLS_PHASE2_PAP);
  esp_eap_client_set_disable_time_check(true);
  esp_wifi_sta_enterprise_enable();
  WiFi.begin(EDUROAM_SSID);
#else
  Serial.printf("Connecting to %s (WPA-Personal)...\n", ssid);
  WiFi.begin(ssid, password);
#endif

  unsigned long wifiStart = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - wifiStart > 30000) {
      Serial.println("WiFi connection timeout!");
      break;
    }
    Serial.print(".");
    delay(500);
  }
  Serial.println();

  lcd.clear();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("WiFi connected! IP: %s\n", WiFi.localIP().toString().c_str());
    lcd.print("WiFi OK");
    lcd.setCursor(0, 1);
    lcd.print(WiFi.localIP().toString());
  } else {
    Serial.printf("WiFi FAILED (final status: %d)\n", WiFi.status());
    lcd.print("WiFi FAILED");
    lcd.setCursor(0, 1);
    lcd.print("Running offline");
  }
  delay(2000);
  lcd.clear();

  ds18b20.requestTemperatures();
  bioTemp   = ds18b20.getTempCByIndex(0);
  waterTemp = ds18b20.getTempCByIndex(1);
  if (bioTemp != DEVICE_DISCONNECTED_C && bioTemp <= 37.5) {
    setMotor(true);
    if (waterTemp < WATER_OFF) setHeater(true);
  }

  Serial.println("System ready");
}

void loop() {
  ds18b20.requestTemperatures();
  bioTemp   = ds18b20.getTempCByIndex(0);
  waterTemp = ds18b20.getTempCByIndex(1);

  // --- 保護 #1: センサー切断チェック ---
  sensorFault = (bioTemp == DEVICE_DISCONNECTED_C ||
                 waterTemp == DEVICE_DISCONNECTED_C);
  if (sensorFault) {
    Serial.println("SENSOR FAULT! Shutting down.");
    setHeater(false);
    setMotor(false);
    lcd.setCursor(0, 2);
    lcd.print("SENSOR FAULT!       ");
    lcd.setCursor(0, 3);
    if (bioTemp == DEVICE_DISCONNECTED_C)   lcd.print("Bio disconnected    ");
    else                                     lcd.print("Water disconnected  ");
    delay(2000);
    return;
  }

  // --- 保護 #2: 絶対上限温度 ---
  if (waterTemp > WATER_ABS_MAX) {
    Serial.println("WATER OVER 85C! Emergency stop.");
    setHeater(false);
    setMotor(false);
    lcd.setCursor(0, 3);
    lcd.print("OVERHEAT EMERGENCY! ");
    delay(2000);
    return;
  }

  // --- 保護 #3: ヒーター連続稼働時間制限 ---
  bool heaterTimeout = (heaterState &&
                        (millis() - heaterOnSince) > HEATER_MAX_MS);
  if (heaterTimeout) {
    Serial.println("HEATER TIMEOUT 60min! Forced OFF.");
    setHeater(false);
    lcd.setCursor(0, 3);
    lcd.print("HEATER TIMEOUT!     ");
    delay(5000);
    // タイマーリセットのため次のサイクルで再ONを許可
  }

  // --- 通常制御 ---
  bool needsHeating;
  if (bioTemp < TEMP_ON)       needsHeating = true;
  else if (bioTemp > TEMP_OFF) needsHeating = false;
  else                         needsHeating = heaterState;

  if (!needsHeating) {
    setHeater(false);
    setMotor(false);
  } else {
    setMotor(true);
    if (heaterTimeout) {
      // タイムアウト直後はOFFを維持、次サイクルで復帰
    } else if (waterTemp > WATER_OFF) {
      setHeater(false);
    } else if (waterTemp < WATER_ON) {
      setHeater(true);
    }
  }

  updateLCD();
  sendToServer();
  delay(5000);
}
