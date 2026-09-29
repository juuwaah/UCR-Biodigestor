#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include "esp_eap_client.h"  // ESP32 Arduino Core 3.x

#define USE_EDUROAM

#include "secrets.h"  // WiFi / eduroam 認証情報（Git管理外）

const char* serverURL = "https://ucr-biodigestor-production.up.railway.app/api/data";

#define ONE_WIRE_BUS 4
#define SSR_HEATER   25
#define LCD_ADDR     0x27

const float TEMP_SET = 37.5;                     // T < 37.5 → ON, それ以外 → OFF
const unsigned long MIN_SWITCH_MS = 60000UL;     // 最低ON/OFF時間 1分

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature ds18b20(&oneWire);
LiquidCrystal_I2C lcd(LCD_ADDR, 20, 4);

bool heaterState = false;
bool sensorFault = false;
float bioTemp    = 0.0;
unsigned long lastSwitch = 0;
bool switchedOnce = false;  // 起動直後は最低時間を待たずに判断する

void setHeater(bool on) {
  if (on != heaterState) {
    lastSwitch = millis();
    switchedOnce = true;
  }
  heaterState = on;
  digitalWrite(SSR_HEATER, on ? HIGH : LOW);
}

void updateLCD() {
  lcd.setCursor(0, 0);
  lcd.printf("Bio:   %5.1f C", bioTemp);
  lcd.setCursor(0, 1);
  lcd.printf("Heater: %s", heaterState ? "ON " : "OFF");
  lcd.setCursor(0, 2);
  lcd.print("                    ");
  lcd.setCursor(0, 3);
  lcd.print("                    ");
}

void sendToServer() {
  if (WiFi.status() != WL_CONNECTED) return;

  char json[96];
  snprintf(json, sizeof(json),
    "{\"biodigester_temp\":%.1f,\"heater\":%s}",
    bioTemp, heaterState ? "true" : "false");

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
  digitalWrite(SSR_HEATER, LOW);

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
  lcd.print("Biodigester v3");
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

  Serial.println("System ready");
}

void loop() {
  ds18b20.requestTemperatures();
  bioTemp = ds18b20.getTempCByIndex(0);

  // --- 保護: センサー切断チェック（最低時間に関係なく即OFF） ---
  sensorFault = (bioTemp == DEVICE_DISCONNECTED_C);
  if (sensorFault) {
    Serial.println("SENSOR FAULT! Heater OFF.");
    if (heaterState) setHeater(false);
    lcd.setCursor(0, 2);
    lcd.print("SENSOR FAULT!       ");
    lcd.setCursor(0, 3);
    lcd.print("Bio disconnected    ");
    delay(2000);
    return;
  }

  // --- 通常制御: T < 37.5 → ON / それ以外 → OFF、切替は最低1分空ける ---
  bool wantHeat = (bioTemp < TEMP_SET);
  bool canSwitch = !switchedOnce || (millis() - lastSwitch >= MIN_SWITCH_MS);
  if (wantHeat != heaterState && canSwitch) setHeater(wantHeat);

  updateLCD();
  sendToServer();
  delay(5000);
}
