# arduino-development

## Структура

Для каждой отдельной программы Arduino создается своя папка.

Все файлы конкретной программы, включая `.ino` и дополнительные скрипты, должны лежать внутри этой папки.

## Программы

- `ds18b20_lcd_i2c` - Arduino Uno, DS18B20, LCD 1602 I2C, встроенный LED D13.
- `sound_volume_analyzer_lcd_i2c` - Arduino Uno, аналоговый звуковой датчик A0, LCD 1602 I2C, встроенный LED D13.
- `nodemcu_ip_weather_clock` - NodeMCU 1.0 ESP-12E, WiFi, NTP, геолокация по IP, OpenWeatherMap, веб-страница с датой, временем и погодой.
- `wemos_ntp_web_clock` - Wemos D1 mini, WiFi, NTP-синхронизация времени, веб-страница с датой и временем.
- `wemos_oled_weather` - Wemos D1 mini, WiFi, OpenWeatherMap, встроенный OLED 128x64 I2C.
- `lcd_backlight_pwm_test` - Wemos D1 mini, blink-тест управления подсветкой LCD на D3/GPIO0.
