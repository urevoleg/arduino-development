# wemos_ntp_web_clock

Скетч для Wemos D1 mini / ESP8266:

- подключается к WiFi;
- синхронизирует время через NTP;
- опрашивает NTP не чаще одного раза в 30 секунд;
- поднимает HTTP-сервер на порту `80`;
- показывает дату и время на веб-странице.

## Настройки

В начале файла `wemos_ntp_web_clock.ino` задайте:

- `WIFI_SSID`
- `WIFI_PASSWORD`
- `NTP_SERVER`
- `TIME_OFFSET_SECONDS`

По умолчанию указано смещение `UTC+3`:

```cpp
const long TIME_OFFSET_SECONDS = 3L * 60L * 60L;
```

## Веб-страница

После загрузки откройте Serial Monitor на скорости `115200` и найдите IP-адрес Wemos.

Откройте в браузере:

```text
http://<ip-address>/
```

Дата выводится в `h3`, время в `h2`:

```html
<h3>03.05.2026</h3>
<h2>14:25:09</h2>
```

Страница обновляется раз в 1 секунду через meta refresh.

## Библиотеки Arduino IDE

Установите поддержку ESP8266 Boards.

Используемые библиотеки:

- ESP8266WiFi
- ESP8266WebServer
- WiFiUdp
- NTPClient

`NTPClient` установите через Library Manager.
