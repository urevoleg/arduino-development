# inner_meteo_module_with_display

Wemos D1 mini sketch for LCD2004 over I2C, a photoresistor on `A0`, WiFi, NTP time, IP geolocation, and OpenWeatherMap weather.

## Hardware used in the first step

- LCD2004 I2C: address `0x27`, `20x4`
- I2C on Wemos D1 mini: `SDA=D2/GPIO4`, `SCL=D1/GPIO5`
- Photoresistor analog input: `LIGHT_PIN=A0`

## Display output

- Page 1: time/date, temperature, humidity, wind, weather text
- Page 2: pressure, EMA-filtered pressure, pressure speed, pressure forecast
- Page 3: geolocation/OpenWeatherMap status, geolocation city, last weather update time, OpenWeatherMap success rate

The display rotates pages every 5 seconds. ASCII pressure graph and wind rose from `nodemcu_ip_weather_clock` are intentionally not included.

## Weather cache

The last successful OpenWeatherMap state is saved to EEPROM and restored on boot. The stored state includes current weather values, weather text, city, the last successful update time, current pressure EMA, and the 3-hour EMA pressure history used for pressure speed/forecast.

EEPROM is updated only after a successful OpenWeatherMap response and no more often than once every 30 minutes. If OpenWeatherMap is temporarily unavailable, the display keeps showing the last available weather state and only the OWM status changes.

OpenWeatherMap request success rate is tracked with two counters for the current 24-hour runtime window: successful requests and failed requests. The counters are saved together with weather state in EEPROM and reset after the 24-hour window expires. After reboot, the 24-hour timer starts again from boot time.

## LCD check

Open Serial Monitor at `115200` baud after upload.

Expected startup output:

```text
Inner meteo module
LCD init started
LCD init done
```

The LCD initialization intentionally follows the working YWROBOT example:

```cpp
Wire.begin(4, 5);
lcd.init();
lcd.backlight();
```

The I2C scanner was used only for diagnostics and is not included in the main sketch.

## Settings

Update these values at the top of the sketch:

```cpp
const char WIFI_SSID[] = "rW";
const char WIFI_PASSWORD[] = "redmiRW2018";
const char OPENWEATHER_API_KEY[] = "your_api_key";
```

Used libraries:

- ESP8266WiFi
- ESP8266HTTPClient
- WiFiUdp
- NTPClient
- ArduinoJson
- LiquidCrystal_I2C
