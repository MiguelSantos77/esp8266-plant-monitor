# esp8266-plant-monitor

This project implements a **Wi-Fi-connected smart irrigation system** using the **ESP8266**, **MQTT**, **EEPROM**, **RTC (DS3231)**, and **OpenWeatherMap API**. It allows for **manual, scheduled, or automatic irrigation** based on soil moisture, time, and temperature data.


## Hardware Requirements

* ESP8266 board (e.g., NodeMCU)
* DS3231 RTC module
* Analog soil moisture sensor
* Relay module (for controlling a water pump/valve)
* Float switch (boia)
* 5V water pump or valve
* Power supply
* Wires, breadboard or PCB


## MQTT Topics

| Topic                   | Description                              | Type     |
| ----------------------- | ---------------------------------------- | -------- |
| `esp/settings`          | Receives configuration in JSON format    | Incoming |
| `esp/commands`          | Manual control commands (e.g., turn on)  | Incoming |
| `watering/humidity`     | Publishes current soil humidity (%)      | Outgoing |
| `watering/lastWatering` | Publishes duration of last watering (ms) | Outgoing |
| `watering/float`        | Publishes water tank status              | Outgoing |

## Example Settings JSON

```json
{
  "mode": "auto",
  "min_humidity": 30,
  "max_humidity": 60,
  "no_water_hours": "",
  "schedule_times": "08:00,20:00",
  "timezone": "WET0WEST,M3.5.0/1,M10.5.0/2",
  "duration": 3000,
  "min_sensor": 280,
  "max_sensor": 655,
  "temperatura_maxima": 35.0,
  "latitude": "38.7169",
  "longitude": "-9.1399"
}
```


## Libraries Used

* `ESP8266WiFi.h`
* `PubSubClient.h` (MQTT)
* `ArduinoJson.h`
* `EEPROM.h`
* `ESP8266HTTPClient.h`
* `Wire.h`
* `RTClib.h`
* `WiFiClientSecure.h`
* `time.h`

## Getting Started

1. Flash the code to an ESP8266-compatible board.
2. Connect the components according to the defined pins:

   * Relay: D1
   * Soil Moisture (analog): A0
   * Float Switch: D2
   * RTC I2C: D3 (SCL), D4 (SDA)
3. Connect to Wi-Fi and MQTT broker (e.g., Mosquitto).
4. Send configuration via MQTT or store it in EEPROM.
5. Monitor logs via Serial and MQTT topics.
