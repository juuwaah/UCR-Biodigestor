#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

const char* ssid     = "b(second)";
const char* password = "yoyoyoyo";
const char* serverURL = "https://ucr-biogestor-production.up.railway.app/api/data";

#define ONE_WIRE_BUS 4
#define SSR_HEATER   25
#define MOTOR_PIN    26
#define LCD_ADDR     0x27

const float TEMP_ON  = 37.0;
const float TEMP_OFF = 38.0;

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature ds18b20(&oneWire);
LiquidCrystal_I2C lcd(LCD_ADDR, 20, 4);

bool heaterState = false;
bool motorState  = false;
float bioTemp    = 0.0;
float waterTemp  = 0.0;

void setOutputs(bool on) {
  heaterState = on;
  motorState  = on;
  digitalWrite(SSR_HEATER, on ? HIGH : LOW);
  digitalWrite(MOTOR_PIN,  on ? HIGH : LOW);
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

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) delay(500);

  lcd.clear();
  lcd.print("WiFi OK");
  lcd.setCursor(0, 1);
  lcd.print(WiFi.localIP().toString());
  delay(2000);
  lcd.clear();

  Serial.println("System ready");
}

void loop() {
  ds18b20.requestTemperatures();
  bioTemp   = ds18b20.getTempCByIndex(0);
  waterTemp = ds18b20.getTempCByIndex(1);

  if (bioTemp == DEVICE_DISCONNECTED_C) {
    Serial.println("Bio sensor error!");
    setOutputs(false);
    lcd.setCursor(0, 3);
    lcd.print("SENSOR ERROR!       ");
    delay(2000);
    return;
  }

  if (!heaterState && bioTemp < TEMP_ON)       setOutputs(true);
  else if (heaterState && bioTemp > TEMP_OFF)  setOutputs(false);

  updateLCD();
  sendToServer();
  delay(5000);
}
