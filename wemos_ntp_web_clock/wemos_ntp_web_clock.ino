#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <time.h>

// Настройки WiFi вынесены сюда, чтобы менять их без правки логики программы.
const char WIFI_SSID[] = "YOUR_WIFI_SSID";
const char WIFI_PASSWORD[] = "YOUR_WIFI_PASSWORD";

const char NTP_SERVER[] = "pool.ntp.org";
const long TIME_OFFSET_SECONDS = 3L * 60L * 60L; // Москва: UTC+3.
const unsigned long NTP_UPDATE_INTERVAL = 30UL * 1000UL;
const unsigned long WIFI_RECONNECT_INTERVAL = 10UL * 1000UL;

ESP8266WebServer server(80);
WiFiUDP ntpUdp;
NTPClient timeClient(ntpUdp, NTP_SERVER, TIME_OFFSET_SECONDS, NTP_UPDATE_INTERVAL);

unsigned long lastWifiReconnectAttempt = 0;
bool timeWasSynchronized = false;

void connectToWifi();
void reconnectWifiIfNeeded();
void initializeWebServer();
void handleRoot();
void handleNotFound();
void updateTimeIfNeeded();
String buildHtmlPage();
String getDateText(time_t currentTime);
String getTimeText(time_t currentTime);
String twoDigits(int value);

void setup() {
  Serial.begin(115200);

  connectToWifi();
  timeClient.begin();

  initializeWebServer();
}

void loop() {
  reconnectWifiIfNeeded();
  updateTimeIfNeeded();
  server.handleClient();
}

void connectToWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.println(F("Connecting to WiFi"));
}

void reconnectWifiIfNeeded() {
  if (WiFi.status() == WL_CONNECTED) {
    return;
  }

  unsigned long now = millis();
  if (now - lastWifiReconnectAttempt < WIFI_RECONNECT_INTERVAL) {
    return;
  }

  lastWifiReconnectAttempt = now;
  Serial.println(F("WiFi reconnecting"));
  WiFi.disconnect();
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

void initializeWebServer() {
  server.on("/", handleRoot);
  server.onNotFound(handleNotFound);
  server.begin();

  Serial.println(F("HTTP server started"));
}

void handleRoot() {
  server.send(200, "text/html; charset=utf-8", buildHtmlPage());
}

void handleNotFound() {
  server.send(404, "text/plain; charset=utf-8", "Not found");
}

void updateTimeIfNeeded() {
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  static bool ipPrinted = false;
  if (!ipPrinted) {
    Serial.print(F("Connected, IP: "));
    Serial.println(WiFi.localIP());
    ipPrinted = true;
  }

  // NTPClient сам ограничивает частоту реального опроса интервалом NTP_UPDATE_INTERVAL.
  if (timeClient.update()) {
    timeWasSynchronized = true;
  }
}

String buildHtmlPage() {
  time_t currentTime = timeClient.getEpochTime();

  String html;
  html.reserve(900);
  html += F("<!doctype html><html lang=\"ru\"><head>");
  html += F("<meta charset=\"utf-8\">");
  html += F("<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">");
  html += F("<meta http-equiv=\"refresh\" content=\"1\">");
  html += F("<title>Wemos Clock</title>");
  html += F("<style>");
  html += F("body{font-family:Arial,sans-serif;margin:0;min-height:100vh;display:grid;place-items:center;background:#f3f5f7;color:#111}");
  html += F("main{text-align:center;padding:24px}");
  html += F("h3{font-size:24px;margin:0 0 12px;font-weight:500}");
  html += F("h2{font-size:54px;margin:0;font-weight:700}");
  html += F("p{color:#666;margin-top:18px}");
  html += F("</style></head><body><main>");

  if (WiFi.status() != WL_CONNECTED) {
    html += F("<h3>WiFi не подключен</h3><h2>--:--:--</h2>");
  } else if (timeWasSynchronized) {
    html += F("<h3>");
    html += getDateText(currentTime);
    html += F("</h3><h2>");
    html += getTimeText(currentTime);
    html += F("</h2>");
  } else {
    html += F("<h3>Время не синхронизировано</h3><h2>--:--:--</h2>");
  }

  html += F("<p>Wemos D1 mini NTP clock</p>");
  html += F("</main></body></html>");
  return html;
}

String getDateText(time_t currentTime) {
  struct tm *timeInfo = localtime(&currentTime);
  if (timeInfo == NULL) {
    return F("--.--.----");
  }

  String result;
  result.reserve(10);
  result += twoDigits(timeInfo->tm_mday);
  result += '.';
  result += twoDigits(timeInfo->tm_mon + 1);
  result += '.';
  result += String(timeInfo->tm_year + 1900);
  return result;
}

String getTimeText(time_t currentTime) {
  struct tm *timeInfo = localtime(&currentTime);
  if (timeInfo == NULL) {
    return F("--:--:--");
  }

  String result;
  result.reserve(8);
  result += twoDigits(timeInfo->tm_hour);
  result += ':';
  result += twoDigits(timeInfo->tm_min);
  result += ':';
  result += twoDigits(timeInfo->tm_sec);
  return result;
}

String twoDigits(int value) {
  if (value < 10) {
    return "0" + String(value);
  }

  return String(value);
}
