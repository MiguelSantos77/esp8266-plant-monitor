#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <time.h>
#include <ArduinoJson.h>
#include <EEPROM.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Wire.h>
#include "RTClib.h"

//Wi-Fi
const char* ssid = "Wi-fi-ssid";
const char* password = "Wi-Fi-password";

// MQTT server
const char* mqtt_server = "mqqt_server_ip";
const int mqtt_port = 1883;

const char* weatherApiKey = "openweathermap-API-key";

// EEPROM config
#define EEPROM_SIZE 1024

// Pins
#define RELAY_PIN D1
#define ANALOG_PIN_HUMIDADE A0
#define FLOAT_SWITCH_PIN D2

WiFiClient espClient;
PubSubClient client(espClient);

RTC_DS3231 rtc;

struct Settings {
  String mode;
  int min_humidity;
  int max_humidity;
  String no_water_hours;
  String schedule_times;
  String timezone;
  int duration;
  int min_sensor;
  int max_sensor;
  float max_temperature;
  String latitude;
  String longitude;
};
Settings currentSettings;

bool isWatering = false;
String lastWateringTime = "";
int lastPublishedHumidity = -1;
bool lastWaterState = true;

void setSystemTimeFromRTC() {
  DateTime now = rtc.now();
  struct tm t;
  t.tm_year = now.year() - 1900;
  t.tm_mon  = now.month() - 1;
  t.tm_mday = now.day();
  t.tm_hour = now.hour();
  t.tm_min  = now.minute();
  t.tm_sec  = now.second();
  time_t timeEpoch = mktime(&t);
  struct timeval tv = { timeEpoch, 0 };
  settimeofday(&tv, nullptr);
  Serial.println("System time set from RTC.");
}

void updateRTCFromSystemTime() {
  time_t now = time(nullptr);
  struct tm* t = localtime(&now);
  rtc.adjust(DateTime(1900 + t->tm_year, t->tm_mon + 1, t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec));
  Serial.println("RTC updated from system time (via NTP).");
}

bool hasWater() {
  return digitalRead(FLOAT_SWITCH_PIN) == LOW;
}

float getCurrentTemperature() {
  if (WiFi.status() != WL_CONNECTED) return -999;
  if (currentSettings.latitude == "" || currentSettings.longitude == "") {
    Serial.println("Latitude or longitude not set. Skipping weather API.");
    return -999;
  }

  WiFiClient client;
  HTTPClient http;

  String url = "http://api.openweathermap.org/data/2.5/weather?lat=" + currentSettings.latitude +
               "&lon=" + currentSettings.longitude +
               "&appid=" + weatherApiKey +
               "&units=metric";

  http.begin(client, url);
  int httpCode = http.GET();

  if (httpCode > 0) {
    String payload = http.getString();
    StaticJsonDocument<1024> doc;
    DeserializationError error = deserializeJson(doc, payload);
    http.end();

    if (!error) {
      float temperature = doc["main"]["temp"];
      Serial.print("Current temperature: ");
      Serial.println(temperature);
      return temperature;
    }
  }
  http.end();
  return -999;
}

void connectWiFi() {
  Serial.print("Connecting to Wi-Fi: ");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected. IP: ");
  Serial.println(WiFi.localIP());
}

void saveSettingsToEEPROM() {
  StaticJsonDocument<512> doc;
  doc["mode"] = currentSettings.mode;
  doc["min_humidity"] = currentSettings.min_humidity;
  doc["max_humidity"] = currentSettings.max_humidity;
  doc["no_water_hours"] = currentSettings.no_water_hours;
  doc["schedule_times"] = currentSettings.schedule_times;
  doc["timezone"] = currentSettings.timezone;
  doc["duration"] = currentSettings.duration;
  doc["min_sensor"] = currentSettings.min_sensor;
  doc["max_sensor"] = currentSettings.max_sensor;
  doc["max_temperature"] = currentSettings.max_temperature;
  doc["latitude"] = currentSettings.latitude;
  doc["longitude"] = currentSettings.longitude;

  char buffer[512];
  size_t len = serializeJson(doc, buffer);

  for (size_t i = 0; i < len; i++) EEPROM.write(i, buffer[i]);
  EEPROM.write(len, '\0');
  EEPROM.commit();
  Serial.println("Settings saved to EEPROM.");
}

void loadSettingsFromEEPROM() {
  char buffer[512];
  for (int i = 0; i < 512; i++) {
    buffer[i] = EEPROM.read(i);
    if (buffer[i] == '\0') break;
  }

  StaticJsonDocument<512> doc;
  DeserializationError error = deserializeJson(doc, buffer);

  if (!error) {
    currentSettings.mode = doc["mode"] | "Manual";
    currentSettings.min_humidity = doc["min_humidity"] | 0;
    currentSettings.max_humidity = doc["max_humidity"] | 1024;
    currentSettings.no_water_hours = doc["no_water_hours"] | "";
    currentSettings.schedule_times = doc["schedule_times"] | "";
    currentSettings.timezone = doc["timezone"] | "";
    currentSettings.duration = doc["duration"] | 1000;
    currentSettings.min_sensor = doc["min_sensor"] | 280;
    currentSettings.max_sensor = doc["max_sensor"] | 655;
    currentSettings.max_temperature = doc["max_temperature"] | 35.0;
    currentSettings.latitude = doc["latitude"] | "";
    currentSettings.longitude = doc["longitude"] | "";
    Serial.println("Settings loaded from EEPROM.");
  } else {
    Serial.println("Failed to load settings from EEPROM.");
  }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String jsonStr;
  for (unsigned int i = 0; i < length; i++) jsonStr += (char)payload[i];

  if (String(topic) == "esp/settings") {
    StaticJsonDocument<4096> doc;
    if (deserializeJson(doc, jsonStr)) return;

    currentSettings.mode = doc["mode"] | "Manual";
    currentSettings.min_humidity = doc["min_humidity"] | 0;
    currentSettings.max_humidity = doc["max_humidity"] | 1024;
    currentSettings.no_water_hours = doc["no_water_hours"] | "";
    currentSettings.schedule_times = doc["schedule_times"] | "";
    currentSettings.timezone = doc["timezone"] | "";
    currentSettings.duration = doc["duration"] | 1000;
    currentSettings.min_sensor = doc["min_sensor"] | 280;
    currentSettings.max_sensor = doc["max_sensor"] | 655;
    currentSettings.max_temperature = doc["max_temperature"] | 150.0;
    currentSettings.latitude = doc["latitude"] | "";
    currentSettings.longitude = doc["longitude"] | "";

    if (currentSettings.timezone != "") {
      configTzTime(currentSettings.timezone.c_str(), "pool.ntp.org");
    }

    saveSettingsToEEPROM();
  }

  if (String(topic) == "esp/commands") {
    StaticJsonDocument<4096> doc;
    if (deserializeJson(doc, jsonStr)) return;
    if (doc["command"] == "startPump" && currentSettings.mode == "Manual") {
      startPump(currentSettings.duration);
    }
  }
}

void reconnectMQTT() {
  while (!client.connected()) {
    String clientId = "ESPClient-" + String(random(0xffff), HEX);
    if (client.connect(clientId.c_str())) {
      client.subscribe("esp/settings");
      client.subscribe("esp/commands");
    } else {
      delay(5000);
    }
  }
}

void publishMQTT(const char* topic, const char* message, bool retain = false) {
  if (client.connected()) client.publish(topic, message, retain);
}

int readHumidity() {
  int analog_output = analogRead(ANALOG_PIN_HUMIDADE);
  int minVal = currentSettings.min_sensor;
  int maxVal = currentSettings.max_sensor;
  if (maxVal <= minVal) return 0;
  int percent = 100 * (maxVal - analog_output) / (maxVal - minVal);
  return constrain(percent, 0, 100);
}

void startPump(int durationMs) {
  float temp = getCurrentTemperature();
  if (temp > currentSettings.max_temperature) {
    Serial.println("Too hot to water. Delaying.");
    return;
  }
  Serial.println("Current temperature:");
  Serial.println(temp);
  if (!hasWater()) {
    Serial.println("No water! Pump not started.");
    publishMQTT("watering/status", "NO_WATER");
    return;
  }

  digitalWrite(RELAY_PIN, LOW);
  delay(durationMs);
  digitalWrite(RELAY_PIN, HIGH);

  char durationStr[10];
  snprintf(durationStr, sizeof(durationStr), "%d", durationMs);
  publishMQTT("watering/lastRun", durationStr);
}

void setup() {
  Serial.begin(115200);

  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, HIGH);
  pinMode(FLOAT_SWITCH_PIN, INPUT_PULLUP);

  Wire.begin(D4, D3); // SDA = D4, SCL = D3

  EEPROM.begin(EEPROM_SIZE);
  loadSettingsFromEEPROM();

  if (!rtc.begin()) {
    Serial.println("RTC not found. Check wiring!");
    while (1) delay(1000);
  }

  connectWiFi();

  if (WiFi.status() == WL_CONNECTED) {
    const char* timezone = currentSettings.timezone != "" ? currentSettings.timezone.c_str() : "WET0WEST,M3.5.0/1,M10.5.0/2";
    configTzTime(timezone, "pool.ntp.org");

    int attempts = 0;
    while (time(nullptr) < 100000 && attempts < 20) {
      delay(500);
      Serial.print(".");
      attempts++;
    }

    if (time(nullptr) >= 100000) {
      updateRTCFromSystemTime();
    } else {
      setSystemTimeFromRTC();
    }
  } else {
    setSystemTimeFromRTC();
  }

  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(mqttCallback);
}

void loop() {
  if (!client.connected()) reconnectMQTT();
  client.loop();

  bool currentWaterState = hasWater();
  if (currentWaterState != lastWaterState) {
    const char* stateStr = currentWaterState ? "HAS_WATER" : "NO_WATER";
    publishMQTT("watering/floater", stateStr, true);
    lastWaterState = currentWaterState;
    Serial.print("Water state changed: ");
    Serial.println(stateStr);
  }

  static unsigned long lastRead = 0;
  if (millis() - lastRead > 10000) {
    lastRead = millis();
    int humidity = readHumidity();
    if (humidity != lastPublishedHumidity) {
      char humStr[10];
      snprintf(humStr, sizeof(humStr), "%d", humidity);
      publishMQTT("watering/humidity", humStr);
      lastPublishedHumidity = humidity;
    }
  }

  if (currentSettings.mode == "schedule") {
    time_t now = time(nullptr);
    struct tm* timeinfo = localtime(&now);
    char currentHour[6];
    snprintf(currentHour, sizeof(currentHour), "%02d:%02d", timeinfo->tm_hour, timeinfo->tm_min);
    String hourStr = String(currentHour);

    String schedule = currentSettings.schedule_times;
    if (schedule != "") {
      char strCopy[schedule.length() + 1];
      schedule.toCharArray(strCopy, sizeof(strCopy));
      char* token = strtok(strCopy, ",");
      while (token != NULL) {
        String scheduledHour = String(token);
        if (scheduledHour == hourStr && hourStr != lastWateringTime) {
          if (readHumidity() < currentSettings.min_humidity) {
            startPump(currentSettings.duration);
            lastWateringTime = hourStr;
          }
        }
        token = strtok(NULL, ",");
      }
    }
  }

  if (currentSettings.mode == "auto") {
    int currentHumidity = readHumidity();
    if (!isWatering && currentHumidity < currentSettings.min_humidity) isWatering = true;
    if (isWatering) {
      if (currentHumidity >= currentSettings.max_humidity) {
        isWatering = false;
      } else {
        startPump(1000);
      }
    }
  }
}
