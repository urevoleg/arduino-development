#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <ArduinoJson.h>
#include <EEPROM.h>
#include <stddef.h>
#include <string.h>
#include <time.h>

// Имя WiFi-сети; строка, без единиц измерения.
const char WIFI_SSID[] = "rW";
// Пароль WiFi-сети; строка, без единиц измерения.
const char WIFI_PASSWORD[] = "redmiRW2018";
// Ключ OpenWeatherMap API; строка, без единиц измерения.
const char OPENWEATHER_API_KEY[] = "your_api_key";

// NTP-сервер времени; строка, без единиц измерения.
const char NTP_SERVER[] = "pool.ntp.org";
// Смещение локального времени от UTC; секунды.
const long TIME_OFFSET_SECONDS = 3L * 60L * 60L;
// Интервал синхронизации времени по NTP; миллисекунды.
const unsigned long NTP_UPDATE_INTERVAL = 30UL * 1000UL;
// Интервал загрузки погоды из OpenWeatherMap; миллисекунды.
const unsigned long WEATHER_UPDATE_INTERVAL = 5UL * 60UL * 1000UL;
// Интервал повторной попытки после ошибки OpenWeatherMap; миллисекунды.
const unsigned long WEATHER_RETRY_INTERVAL = 30UL * 1000UL;
// Интервал обновления геолокации по внешнему IP; миллисекунды.
const unsigned long GEOLOCATION_UPDATE_INTERVAL = 6UL * 60UL * 60UL * 1000UL;
// Интервал попыток переподключения к WiFi; миллисекунды.
const unsigned long WIFI_RECONNECT_INTERVAL = 10UL * 1000UL;
// Период heartbeat-вспышки status LED; миллисекунды.
const unsigned long STATUS_LED_BLINK_INTERVAL = 1000UL;
// Длительность включения status LED внутри heartbeat-вспышки; миллисекунды.
const unsigned long STATUS_LED_ON_DURATION = 100UL;
// Пин status LED; номер GPIO, GPIO2 = D4 на NodeMCU.
const byte STATUS_LED_PIN = 2;
// Координаты прогноза по умолчанию, если геолокация по IP еще не получена.
const float DEFAULT_WEATHER_LATITUDE = 55.567586;
const float DEFAULT_WEATHER_LONGITUDE = 38.225004;
// Название города по умолчанию для отображения.
const char DEFAULT_WEATHER_CITY[] = "Ramenskoye";
// Коэффициент EMA-фильтра давления: 0.0..1.0, больше = быстрее реакция на новое значение.
const float PRESSURE_SMOOTHING_K = 0.3;
// Длительность истории сглаженных сэмплов давления; миллисекунды.
const unsigned long PRESSURE_HISTORY_WINDOW = 3UL * 60UL * 60UL * 1000UL;
// Количество сглаженных сэмплов давления в истории за 3 часа; штуки.
const byte PRESSURE_HISTORY_COUNT = PRESSURE_HISTORY_WINDOW / WEATHER_UPDATE_INTERVAL;
// Сдвиг для сравнения давления с прошлым значением; количество погодных сэмплов.
const byte PRESSURE_COMPARE_SAMPLE_OFFSET = 30UL * 60UL * 1000UL / WEATHER_UPDATE_INTERVAL;
// Порог быстрого изменения давления; мм рт. ст. в час.
const float PRESSURE_FAST_SPEED_THRESHOLD = 1.5;
// Порог обычного изменения давления; мм рт. ст. в час.
const float PRESSURE_SLOW_SPEED_THRESHOLD = 0.2;
// Шаг вертикальной шкалы ASCII-графика давления; мм рт. ст.
const float PRESSURE_GRAPH_STEP = 0.1;
// Максимальная высота ASCII-графика давления; строки.
const byte PRESSURE_GRAPH_MAX_ROWS = 30;
// Магическое число блока EEPROM с историей давления; без единиц измерения.
const uint32_t EEPROM_MAGIC = 0x50484B57UL;
// Версия формата блока EEPROM; без единиц измерения.
const byte EEPROM_VERSION = 4;
// Шаг сектора статистики ветра; градусы.
const byte WIND_DIRECTION_BUCKET_DEGREES = 10;
// Количество секторов статистики ветра; штуки.
const byte WIND_DIRECTION_BUCKET_COUNT = 360 / WIND_DIRECTION_BUCKET_DEGREES;
// Размер ASCII-поля розы ветров; символы.
const byte WIND_ROSE_SIZE = 21;
// Радиус ASCII-розы ветров; символы.
const byte WIND_ROSE_RADIUS = WIND_ROSE_SIZE / 2;
// Длительность скользящего окна статистики ветра; миллисекунды.
const unsigned long WIND_HISTORY_WINDOW = 24UL * 60UL * 60UL * 1000UL;
// Количество записей ветра в скользящем окне за 24 часа; штуки.
const uint16_t WIND_HISTORY_COUNT = WIND_HISTORY_WINDOW / WEATHER_UPDATE_INTERVAL;

struct PressureHistoryStorage {
  uint32_t magic;
  byte version;
  byte writeIndex;
  byte storedCount;
  float history[PRESSURE_HISTORY_COUNT];
  uint16_t windHistory[WIND_HISTORY_COUNT];
  uint16_t windWriteIndex;
  uint16_t windStoredCount;
  uint16_t checksum;
};

// Размер эмулируемой EEPROM; байты.
const size_t EEPROM_SIZE = sizeof(PressureHistoryStorage);
PressureHistoryStorage pressureHistoryStorageBuffer;
StaticJsonDocument<1024> geolocationJsonDoc;
StaticJsonDocument<1536> weatherJsonDoc;
float pressureGraphValues[PRESSURE_HISTORY_COUNT];
char pressureAxisLabels[PRESSURE_HISTORY_COUNT + 1];
uint16_t windDirectionCounts[WIND_DIRECTION_BUCKET_COUNT];
char windRoseGrid[WIND_ROSE_SIZE][WIND_ROSE_SIZE + 1];

ESP8266WebServer server(80);
WiFiUDP ntpUdp;
NTPClient timeClient(ntpUdp, NTP_SERVER, TIME_OFFSET_SECONDS, NTP_UPDATE_INTERVAL);

unsigned long lastWifiReconnectAttempt = 0;
unsigned long lastGeolocationUpdate = 0;
unsigned long lastWeatherUpdate = 0;
unsigned long lastStatusLedBlink = 0;
bool statusLedActive = false;
bool timeWasSynchronized = false;
bool geolocationWasLoaded = false;
bool geolocationLastUpdateOk = false;
bool weatherWasLoaded = false;
bool weatherLastUpdateOk = false;
float weatherLatitude = DEFAULT_WEATHER_LATITUDE;
float weatherLongitude = DEFAULT_WEATHER_LONGITUDE;
String weatherCity = DEFAULT_WEATHER_CITY;
String geolocationCity = "";
String geolocationCountry = "";
String geolocationPublicIp = "";
String geolocationError = "";
String geolocationStatusText = "ожидание первого обновления";
float weatherTemperature = 0.0;
float weatherFeelsLike = 0.0;
float weatherPressureMmHg = 0.0;
int weatherHumidity = 0;
float weatherWindSpeed = 0.0;
int weatherWindDeg = 0;
String weatherMain = "";
String weatherDescription = "";
String weatherError = "";
String weatherStatusText = "ожидание первого обновления";
float smoothedPressureMmHg = 0.0;
bool smoothedPressureReady = false;
float pressureHistory[PRESSURE_HISTORY_COUNT];
byte pressureHistoryWriteIndex = 0;
byte pressureHistoryStoredCount = 0;
bool pressureHistoryLoadedFromStorage = false;
uint16_t windHistory[WIND_HISTORY_COUNT];
uint16_t windWriteIndex = 0;
uint16_t windStoredCount = 0;

void connectToWifi();
void reconnectWifiIfNeeded();
void initializeWebServer();
void handleRoot();
void handleNotFound();
void updateTimeIfNeeded();
void updateGeolocationIfNeeded();
void updateWeatherIfNeeded();
void updateStatusLed();
bool fetchGeolocation();
String buildGeolocationUrl();
bool parseGeolocationJson(const String &payload);
bool fetchWeather();
String buildWeatherUrl();
bool parseWeatherJson(const String &payload);
float hpaToMmHg(float pressureHpa);
void addPressureSample(float pressureMmHg);
void storeFilteredPressure(float pressureMmHg);
void addWindDirectionSample(int windDeg);
void initializePressureHistoryIfEmpty(float pressureMmHg);
bool loadPressureHistoryFromEeprom();
void savePressureHistoryToEeprom();
uint16_t calculateStorageChecksum(const PressureHistoryStorage &storage);
bool getPressureHistoryByAge(byte ageFromNewest, float *pressureMmHg);
bool getCurrentFilteredPressure(float *pressureMmHg);
bool getPressureSpeed(float *speedMmHgPerHour);
byte getPressureForecastLevel(float speedMmHgPerHour);
const char *getPressureForecastIcon(byte level);
const char *getPressureForecastText(byte level);
const char *getPressureForecastClass(byte level);
String buildPressureAsciiGraph();
String buildWindRoseAsciiGraph();
void setAxisLabel(char *axisLabels, byte width, byte start, const char *label);
String buildHtmlPage();
String getDateText(time_t currentTime);
String getTimeText(time_t currentTime);
String twoDigits(int value);
const char *getMonthName(int monthIndex);
const char *getWeekdayName(int weekdayIndex);

void setup() {
  pinMode(STATUS_LED_PIN, OUTPUT);
  digitalWrite(STATUS_LED_PIN, LOW);

  Serial.begin(115200);

  EEPROM.begin(EEPROM_SIZE);
  pressureHistoryLoadedFromStorage = loadPressureHistoryFromEeprom();
  Serial.println(pressureHistoryLoadedFromStorage ? F("Pressure history loaded from EEPROM") : F("Pressure history EEPROM is empty"));

  connectToWifi();
  timeClient.begin();

  initializeWebServer();
}

void loop() {
  updateStatusLed();
  reconnectWifiIfNeeded();
  updateTimeIfNeeded();
  updateGeolocationIfNeeded();
  updateWeatherIfNeeded();
  server.handleClient();
}

void updateStatusLed() {
  unsigned long now = millis();

  if (statusLedActive && now - lastStatusLedBlink >= STATUS_LED_ON_DURATION) {
    statusLedActive = false;
    digitalWrite(STATUS_LED_PIN, LOW);
    return;
  }

  if (!statusLedActive && now - lastStatusLedBlink >= STATUS_LED_BLINK_INTERVAL) {
    lastStatusLedBlink = now;
    statusLedActive = true;
    digitalWrite(STATUS_LED_PIN, HIGH);
  }
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

void updateWeatherIfNeeded() {
  if (WiFi.status() != WL_CONNECTED) {
    weatherLastUpdateOk = false;
    weatherStatusText = F("WiFi не подключен");
    return;
  }

  unsigned long now = millis();
  unsigned long weatherInterval = weatherLastUpdateOk ? WEATHER_UPDATE_INTERVAL : WEATHER_RETRY_INTERVAL;
  if (lastWeatherUpdate != 0 && now - lastWeatherUpdate < weatherInterval) {
    return;
  }

  lastWeatherUpdate = now;
  weatherLastUpdateOk = fetchWeather();
  if (weatherLastUpdateOk) {
    weatherWasLoaded = true;
    weatherStatusText = F("OK");
  }
}

void updateGeolocationIfNeeded() {
  if (WiFi.status() != WL_CONNECTED) {
    geolocationLastUpdateOk = false;
    geolocationStatusText = F("WiFi не подключен");
    return;
  }

  unsigned long now = millis();
  if (lastGeolocationUpdate != 0 && now - lastGeolocationUpdate < GEOLOCATION_UPDATE_INTERVAL) {
    return;
  }

  lastGeolocationUpdate = now;
  geolocationLastUpdateOk = fetchGeolocation();
  if (geolocationLastUpdateOk) {
    geolocationWasLoaded = true;
    geolocationStatusText = F("OK");
    lastWeatherUpdate = 0;
  }
}

bool fetchGeolocation() {
  WiFiClient client;
  HTTPClient http;
  String url = buildGeolocationUrl();

  if (!http.begin(client, url)) {
    geolocationError = F("HTTP init failed");
    geolocationStatusText = geolocationError;
    return false;
  }

  int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    geolocationError = F("HTTP ");
    geolocationError += String(httpCode);
    geolocationStatusText = geolocationError;
    http.end();
    return false;
  }

  String payload = http.getString();
  http.end();
  return parseGeolocationJson(payload);
}

String buildGeolocationUrl() {
  return F("http://ip-api.com/json/?fields=status,message,country,city,lat,lon,query");
}

bool parseGeolocationJson(const String &payload) {
  geolocationJsonDoc.clear();
  DeserializationError error = deserializeJson(geolocationJsonDoc, payload);
  if (error) {
    geolocationError = F("JSON parse failed");
    geolocationStatusText = geolocationError;
    return false;
  }

  const char *status = geolocationJsonDoc["status"] | "";
  if (strcmp(status, "success") != 0) {
    geolocationError = geolocationJsonDoc["message"] | "IP geolocation failed";
    geolocationStatusText = geolocationError;
    return false;
  }

  weatherLatitude = geolocationJsonDoc["lat"] | DEFAULT_WEATHER_LATITUDE;
  weatherLongitude = geolocationJsonDoc["lon"] | DEFAULT_WEATHER_LONGITUDE;
  geolocationCity = geolocationJsonDoc["city"] | "";
  geolocationCountry = geolocationJsonDoc["country"] | "";
  geolocationPublicIp = geolocationJsonDoc["query"] | "";
  if (geolocationCity.length() > 0) {
    weatherCity = geolocationCity;
  } else {
    weatherCity = DEFAULT_WEATHER_CITY;
  }
  geolocationError = "";
  geolocationStatusText = F("OK");

  Serial.print(F("IP geolocation: "));
  Serial.print(weatherCity);
  Serial.print(F(", "));
  Serial.print(weatherLatitude, 6);
  Serial.print(F(", "));
  Serial.println(weatherLongitude, 6);
  return true;
}

bool fetchWeather() {
  WiFiClient client;
  HTTPClient http;
  String url = buildWeatherUrl();

  if (!http.begin(client, url)) {
    weatherError = F("HTTP init failed");
    weatherStatusText = weatherError;
    return false;
  }

  int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    weatherError = F("HTTP ");
    weatherError += String(httpCode);
    weatherStatusText = weatherError;
    http.end();
    return false;
  }

  String payload = http.getString();
  http.end();
  return parseWeatherJson(payload);
}

String buildWeatherUrl() {
  String url;
  url.reserve(180);
  url += F("http://api.openweathermap.org/data/2.5/weather?lat=");
  url += String(weatherLatitude, 6);
  url += F("&lon=");
  url += String(weatherLongitude, 6);
  url += F("&appid=");
  url += OPENWEATHER_API_KEY;
  url += F("&units=metric&lang=ru");
  return url;
}

bool parseWeatherJson(const String &payload) {
  weatherJsonDoc.clear();
  DeserializationError error = deserializeJson(weatherJsonDoc, payload);
  if (error) {
    weatherError = F("JSON parse failed");
    weatherStatusText = weatherError;
    return false;
  }

  weatherTemperature = weatherJsonDoc["main"]["temp"] | 0.0;
  weatherFeelsLike = weatherJsonDoc["main"]["feels_like"] | 0.0;
  weatherHumidity = weatherJsonDoc["main"]["humidity"] | 0;
  weatherPressureMmHg = hpaToMmHg(weatherJsonDoc["main"]["pressure"] | 0.0);
  weatherWindSpeed = weatherJsonDoc["wind"]["speed"] | 0.0;
  weatherWindDeg = weatherJsonDoc["wind"]["deg"] | 0;
  weatherMain = weatherJsonDoc["weather"][0]["main"] | "";
  weatherDescription = weatherJsonDoc["weather"][0]["description"] | "";
  const char *openWeatherName = weatherJsonDoc["name"] | "";
  if (openWeatherName[0] != '\0') {
    weatherCity = openWeatherName;
  }
  weatherError = "";
  weatherStatusText = F("OK");
  addPressureSample(weatherPressureMmHg);
  addWindDirectionSample(weatherWindDeg);
  return true;
}

float hpaToMmHg(float pressureHpa) {
  return pressureHpa * 0.750062;
}

void addPressureSample(float pressureMmHg) {
  if (smoothedPressureReady) {
    smoothedPressureMmHg = pressureMmHg * PRESSURE_SMOOTHING_K + smoothedPressureMmHg * (1.0 - PRESSURE_SMOOTHING_K);
  } else {
    smoothedPressureMmHg = pressureMmHg;
    smoothedPressureReady = true;
  }

  initializePressureHistoryIfEmpty(smoothedPressureMmHg);
  storeFilteredPressure(smoothedPressureMmHg);
}

void storeFilteredPressure(float pressureMmHg) {
  pressureHistory[pressureHistoryWriteIndex] = pressureMmHg;
  pressureHistoryWriteIndex = (pressureHistoryWriteIndex + 1) % PRESSURE_HISTORY_COUNT;

  if (pressureHistoryStoredCount < PRESSURE_HISTORY_COUNT) {
    pressureHistoryStoredCount++;
  }

  // EEPROM сохраняется после каждого успешного обновления ветра вместе со всем storage-блоком.
}

void addWindDirectionSample(int windDeg) {
  int normalizedDeg = windDeg % 360;
  if (normalizedDeg < 0) {
    normalizedDeg += 360;
  }

  windHistory[windWriteIndex] = normalizedDeg;
  windWriteIndex = (windWriteIndex + 1) % WIND_HISTORY_COUNT;
  if (windStoredCount < WIND_HISTORY_COUNT) {
    windStoredCount++;
  }

  savePressureHistoryToEeprom();
}

void initializePressureHistoryIfEmpty(float pressureMmHg) {
  if (pressureHistoryStoredCount > 0) {
    return;
  }

  for (byte i = 0; i < PRESSURE_HISTORY_COUNT; i++) {
    pressureHistory[i] = pressureMmHg;
  }
  pressureHistoryWriteIndex = 0;
  pressureHistoryStoredCount = PRESSURE_HISTORY_COUNT;
  smoothedPressureMmHg = pressureMmHg;
  smoothedPressureReady = true;
  savePressureHistoryToEeprom();
}

bool loadPressureHistoryFromEeprom() {
  EEPROM.get(0, pressureHistoryStorageBuffer);

  if (pressureHistoryStorageBuffer.magic != EEPROM_MAGIC || pressureHistoryStorageBuffer.version != EEPROM_VERSION) {
    return false;
  }
  if (pressureHistoryStorageBuffer.writeIndex >= PRESSURE_HISTORY_COUNT || pressureHistoryStorageBuffer.storedCount > PRESSURE_HISTORY_COUNT) {
    return false;
  }
  if (pressureHistoryStorageBuffer.windWriteIndex >= WIND_HISTORY_COUNT || pressureHistoryStorageBuffer.windStoredCount > WIND_HISTORY_COUNT) {
    return false;
  }
  if (pressureHistoryStorageBuffer.checksum != calculateStorageChecksum(pressureHistoryStorageBuffer)) {
    return false;
  }

  pressureHistoryWriteIndex = pressureHistoryStorageBuffer.writeIndex;
  pressureHistoryStoredCount = pressureHistoryStorageBuffer.storedCount;
  for (byte i = 0; i < PRESSURE_HISTORY_COUNT; i++) {
    pressureHistory[i] = pressureHistoryStorageBuffer.history[i];
  }
  if (pressureHistoryStoredCount > 0) {
    byte newestAge = 0;
    getPressureHistoryByAge(newestAge, &smoothedPressureMmHg);
    smoothedPressureReady = true;
  }
  windWriteIndex = pressureHistoryStorageBuffer.windWriteIndex;
  windStoredCount = pressureHistoryStorageBuffer.windStoredCount;
  for (uint16_t i = 0; i < WIND_HISTORY_COUNT; i++) {
    windHistory[i] = pressureHistoryStorageBuffer.windHistory[i];
  }

  return pressureHistoryStoredCount > 0;
}

void savePressureHistoryToEeprom() {
  memset(&pressureHistoryStorageBuffer, 0, sizeof(pressureHistoryStorageBuffer));

  pressureHistoryStorageBuffer.magic = EEPROM_MAGIC;
  pressureHistoryStorageBuffer.version = EEPROM_VERSION;
  pressureHistoryStorageBuffer.writeIndex = pressureHistoryWriteIndex;
  pressureHistoryStorageBuffer.storedCount = pressureHistoryStoredCount;
  for (byte i = 0; i < PRESSURE_HISTORY_COUNT; i++) {
    pressureHistoryStorageBuffer.history[i] = pressureHistory[i];
  }
  pressureHistoryStorageBuffer.windWriteIndex = windWriteIndex;
  pressureHistoryStorageBuffer.windStoredCount = windStoredCount;
  for (uint16_t i = 0; i < WIND_HISTORY_COUNT; i++) {
    pressureHistoryStorageBuffer.windHistory[i] = windHistory[i];
  }
  pressureHistoryStorageBuffer.checksum = calculateStorageChecksum(pressureHistoryStorageBuffer);

  EEPROM.put(0, pressureHistoryStorageBuffer);
  EEPROM.commit();
}

uint16_t calculateStorageChecksum(const PressureHistoryStorage &storage) {
  const byte *data = reinterpret_cast<const byte *>(&storage);
  size_t checksumOffset = offsetof(PressureHistoryStorage, checksum);
  uint16_t checksum = 0;

  for (size_t i = 0; i < checksumOffset; i++) {
    checksum = (checksum * 31) + data[i];
  }

  return checksum;
}

bool getPressureHistoryByAge(byte ageFromNewest, float *pressureMmHg) {
  if (ageFromNewest >= pressureHistoryStoredCount) {
    return false;
  }

  int index = pressureHistoryWriteIndex - 1 - ageFromNewest;
  while (index < 0) {
    index += PRESSURE_HISTORY_COUNT;
  }

  *pressureMmHg = pressureHistory[index];
  return true;
}

bool getCurrentFilteredPressure(float *pressureMmHg) {
  if (smoothedPressureReady) {
    *pressureMmHg = smoothedPressureMmHg;
    return true;
  }

  return getPressureHistoryByAge(0, pressureMmHg);
}

bool getPressureSpeed(float *speedMmHgPerHour) {
  float currentPressure = 0.0;
  float previousPressure = 0.0;

  if (!getCurrentFilteredPressure(&currentPressure)) {
    return false;
  }
  if (!getPressureHistoryByAge(PRESSURE_COMPARE_SAMPLE_OFFSET - 1, &previousPressure)) {
    return false;
  }

  float deltaMmHg = currentPressure - previousPressure;
  *speedMmHgPerHour = deltaMmHg * 2.0;
  return true;
}

byte getPressureForecastLevel(float speedMmHgPerHour) {
  if (speedMmHgPerHour <= -PRESSURE_FAST_SPEED_THRESHOLD) {
    return 0;
  }
  if (speedMmHgPerHour <= -PRESSURE_SLOW_SPEED_THRESHOLD) {
    return 1;
  }
  if (speedMmHgPerHour >= PRESSURE_FAST_SPEED_THRESHOLD) {
    return 4;
  }
  if (speedMmHgPerHour >= PRESSURE_SLOW_SPEED_THRESHOLD) {
    return 3;
  }

  return 2;
}

const char *getPressureForecastIcon(byte level) {
  const char *icons[] = { "▼▼", "▼", "■", "▲", "▲▲" };
  if (level > 4) {
    return "?";
  }

  return icons[level];
}

const char *getPressureForecastText(byte level) {
  const char *texts[] = {
    "быстро падает",
    "падает",
    "стабильно",
    "растет",
    "быстро растет"
  };
  if (level > 4) {
    return "нет данных";
  }

  return texts[level];
}

const char *getPressureForecastClass(byte level) {
  const char *classes[] = { "fall-fast", "fall", "stable", "rise", "rise-fast" };
  if (level > 4) {
    return "stable";
  }

  return classes[level];
}

String buildPressureAsciiGraph() {
  if (pressureHistoryStoredCount == 0) {
    return F("нет данных");
  }

  float minPressure = 0.0;
  float maxPressure = 0.0;
  byte pointCount = 0;

  for (int age = pressureHistoryStoredCount - 1; age >= 0; age--) {
    float pressureMmHg = 0.0;
    if (!getPressureHistoryByAge(age, &pressureMmHg)) {
      continue;
    }

    pressureGraphValues[pointCount] = pressureMmHg;
    if (pointCount == 0 || pressureMmHg < minPressure) {
      minPressure = pressureMmHg;
    }
    if (pointCount == 0 || pressureMmHg > maxPressure) {
      maxPressure = pressureMmHg;
    }
    pointCount++;
  }

  if (pointCount == 0) {
    return F("нет данных");
  }

  int minLevel = floor(minPressure / PRESSURE_GRAPH_STEP);
  int maxLevel = ceil(maxPressure / PRESSURE_GRAPH_STEP);
  int levelStep = 1;
  int rowCount = maxLevel - minLevel + 1;
  while (rowCount > PRESSURE_GRAPH_MAX_ROWS) {
    levelStep++;
    rowCount = (maxLevel - minLevel) / levelStep + 1;
  }

  String graph;
  graph.reserve((pointCount + 10) * rowCount + pointCount + 42);

  for (int level = maxLevel; level >= minLevel; level -= levelStep) {
    float rowPressure = level * PRESSURE_GRAPH_STEP;
    graph += String(rowPressure, 1);
    graph += F(" |");

    for (byte i = 0; i < pointCount; i++) {
      int pointLevel = round(pressureGraphValues[i] / PRESSURE_GRAPH_STEP);
      graph += (pointLevel >= level && pointLevel < level + levelStep) ? '*' : ' ';
    }
    graph += '\n';
  }

  graph += F("      +");
  for (byte i = 0; i < pointCount; i++) {
    graph += '-';
  }

  for (byte i = 0; i < pointCount; i++) {
    pressureAxisLabels[i] = ' ';
  }
  pressureAxisLabels[pointCount] = '\0';
  setAxisLabel(pressureAxisLabels, pointCount, 0, "-3h");
  setAxisLabel(pressureAxisLabels, pointCount, pointCount / 3, "-2h");
  setAxisLabel(pressureAxisLabels, pointCount, (pointCount * 2) / 3, "-1h");
  setAxisLabel(pressureAxisLabels, pointCount, pointCount - 1, "0");

  graph += F("\n       ");
  graph += pressureAxisLabels;
  return graph;
}

void setAxisLabel(char *axisLabels, byte width, byte start, const char *label) {
  if (width == 0) {
    return;
  }

  byte labelLength = strlen(label);
  if (labelLength == 0 || labelLength > width) {
    return;
  }
  if (start + labelLength > width) {
    start = width - labelLength;
  }

  for (byte i = 0; i < labelLength; i++) {
    axisLabels[start + i] = label[i];
  }
}

String buildWindRoseAsciiGraph() {
  memset(windDirectionCounts, 0, sizeof(windDirectionCounts));

  for (uint16_t i = 0; i < windStoredCount; i++) {
    uint16_t index = (windWriteIndex + WIND_HISTORY_COUNT - windStoredCount + i) % WIND_HISTORY_COUNT;
    uint16_t windDeg = windHistory[index];
    byte bucket = (windDeg + WIND_DIRECTION_BUCKET_DEGREES / 2) / WIND_DIRECTION_BUCKET_DEGREES;
    if (bucket >= WIND_DIRECTION_BUCKET_COUNT) {
      bucket = 0;
    }
    if (windDirectionCounts[bucket] < 65535) {
      windDirectionCounts[bucket]++;
    }
  }

  uint16_t maxCount = 0;
  uint32_t totalCount = 0;

  for (byte i = 0; i < WIND_DIRECTION_BUCKET_COUNT; i++) {
    if (windDirectionCounts[i] > maxCount) {
      maxCount = windDirectionCounts[i];
    }
    totalCount += windDirectionCounts[i];
  }

  if (maxCount == 0) {
    return F("нет данных");
  }

  for (byte y = 0; y < WIND_ROSE_SIZE; y++) {
    for (byte x = 0; x < WIND_ROSE_SIZE; x++) {
      windRoseGrid[y][x] = ' ';
    }
    windRoseGrid[y][WIND_ROSE_SIZE] = '\0';
  }

  byte center = WIND_ROSE_RADIUS;
  windRoseGrid[center][center] = '+';
  windRoseGrid[0][center] = 'N';
  windRoseGrid[center][WIND_ROSE_SIZE - 1] = 'E';
  windRoseGrid[WIND_ROSE_SIZE - 1][center] = 'S';
  windRoseGrid[center][0] = 'W';

  for (byte bucket = 0; bucket < WIND_DIRECTION_BUCKET_COUNT; bucket++) {
    uint16_t count = windDirectionCounts[bucket];
    if (count == 0) {
      continue;
    }

    float angleRad = radians(bucket * WIND_DIRECTION_BUCKET_DEGREES);
    byte length = round((float)count * WIND_ROSE_RADIUS / maxCount);
    if (length == 0) {
      length = 1;
    }

    for (byte step = 1; step <= length; step++) {
      int x = center + round(sin(angleRad) * step);
      int y = center - round(cos(angleRad) * step);
      if (x < 0 || x >= WIND_ROSE_SIZE || y < 0 || y >= WIND_ROSE_SIZE) {
        continue;
      }
      if (windRoseGrid[y][x] == ' ' || windRoseGrid[y][x] == '+' || windRoseGrid[y][x] == 'N' || windRoseGrid[y][x] == 'E' || windRoseGrid[y][x] == 'S' || windRoseGrid[y][x] == 'W') {
        windRoseGrid[y][x] = '*';
      }
    }
  }

  windRoseGrid[0][center] = 'N';
  windRoseGrid[center][WIND_ROSE_SIZE - 1] = 'E';
  windRoseGrid[WIND_ROSE_SIZE - 1][center] = 'S';
  windRoseGrid[center][0] = 'W';
  windRoseGrid[center][center] = '+';

  String graph;
  graph.reserve((WIND_ROSE_SIZE + 1) * WIND_ROSE_SIZE + 48);
  for (byte y = 0; y < WIND_ROSE_SIZE; y++) {
    graph += windRoseGrid[y];
    graph += '\n';
  }
  graph += F("samples: ");
  graph += String(totalCount);
  graph += F(", max sector: ");
  graph += String(maxCount);
  return graph;
}

String buildHtmlPage() {
  time_t currentTime = timeClient.getEpochTime();
  float filteredPressureMmHg = 0.0;
  float pressureSpeedMmHgPerHour = 0.0;
  bool hasFilteredPressure = getCurrentFilteredPressure(&filteredPressureMmHg);
  bool hasPressureSpeed = getPressureSpeed(&pressureSpeedMmHgPerHour);
  byte pressureForecastLevel = hasPressureSpeed ? getPressureForecastLevel(pressureSpeedMmHgPerHour) : 2;

  String html;
  html.reserve(3000);
  html += F("<!doctype html><html lang=\"ru\"><head>");
  html += F("<meta charset=\"utf-8\">");
  html += F("<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">");
  html += F("<title>NodeMCU Weather Clock</title>");
  html += F("<style>");
  html += F("body{font-family:Arial,sans-serif;margin:0;min-height:100vh;display:grid;place-items:center;background:#f3f5f7;color:#111}");
  html += F("main{text-align:center;padding:24px}");
  html += F("h3{font-size:24px;margin:0 0 12px;font-weight:500}");
  html += F("h2{font-size:54px;margin:0;font-weight:700}");
  html += F("section{margin-top:24px;text-align:left;display:inline-block}");
  html += F("p{color:#444;margin:8px 0;font-size:18px}");
  html += F("pre{font:18px/1.2 monospace;margin:8px 0;color:#222;white-space:pre}");
  html += F(".ok{color:#2e7d32}");
  html += F(".error{color:#b71c1c}");
  html += F(".forecast{font-weight:700}");
  html += F(".forecast span{display:inline-block;min-width:38px;text-align:center}");
  html += F(".fall-fast{color:#b71c1c}");
  html += F(".fall{color:#e65100}");
  html += F(".stable{color:#546e7a}");
  html += F(".rise{color:#2e7d32}");
  html += F(".rise-fast{color:#00695c}");
  html += F(".muted{color:#666;margin-top:18px;font-size:14px;text-align:center}");
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

  html += F("<section>");
  html += F("<p><b>Город погоды:</b> ");
  html += weatherCity;
  html += F("</p><p><b>Координаты погоды:</b> ");
  html += String(weatherLatitude, 6);
  html += F(", ");
  html += String(weatherLongitude, 6);
  html += F("</p><p><b>Геолокация по IP:</b> <span class=\"");
  html += (geolocationLastUpdateOk ? F("ok") : F("error"));
  html += F("\">");
  html += geolocationStatusText;
  html += F("</span>");
  if (geolocationWasLoaded) {
    html += F(" - ");
    if (geolocationCity.length() > 0) {
      html += geolocationCity;
    } else {
      html += F("город не определен");
    }
    if (geolocationCountry.length() > 0) {
      html += F(", ");
      html += geolocationCountry;
    }
    if (geolocationPublicIp.length() > 0) {
      html += F(" (IP ");
      html += geolocationPublicIp;
      html += F(")");
    }
  }
  html += F("</p>");
  html += F("<p><b>OpenWeatherMap:</b> <span class=\"");
  html += (weatherLastUpdateOk ? F("ok") : F("error"));
  html += F("\">");
  html += weatherStatusText;
  html += F("</span></p>");
  html += F("<p><b>Погода:</b> ");
  if (weatherWasLoaded) {
    html += weatherMain;
    html += F(", ");
    html += weatherDescription;
    html += F("</p><p><b>Температура:</b> ");
    html += String(weatherTemperature, 1);
    html += F(" °C, ощущается ");
    html += String(weatherFeelsLike, 1);
    html += F(" °C");
  } else {
    html += F("нет данных</p><p><b>Температура:</b> нет данных");
  }

  html += F("</p>");

  if (weatherWasLoaded) {
    html += F("<p><b>Влажность:</b> ");
    html += String(weatherHumidity);
    html += F("%</p><p><b>Давление:</b> ");
    html += String(weatherPressureMmHg, 1);
    html += F(" мм рт. ст.</p><p><b>Давление, EMA-фильтр:</b> ");
    if (hasFilteredPressure) {
      html += String(filteredPressureMmHg, 1);
      html += F(" мм рт. ст.");
    } else {
      html += F("нет данных");
    }
    html += F("</p><p><b>Скорость давления:</b> ");
    if (hasPressureSpeed) {
      html += String(pressureSpeedMmHgPerHour, 1);
      html += F(" мм рт. ст./ч");
    } else {
      html += F("нужно 30 минут истории");
    }
    html += F("</p><p class=\"forecast ");
    html += getPressureForecastClass(pressureForecastLevel);
    html += F("\"><b>Прогноз:</b> <span>");
    if (hasPressureSpeed) {
      html += getPressureForecastIcon(pressureForecastLevel);
      html += F("</span> ");
      html += getPressureForecastText(pressureForecastLevel);
    } else {
      html += F("?</span> сбор данных");
    }
    html += F("</p><p><b>График сглаженного давления:</b></p><pre>");
    html += buildPressureAsciiGraph();
    html += F("</pre>");
    html += F("<p><b>Ветер:</b> ");
    html += String(weatherWindSpeed, 1);
    html += F(" м/с, ");
    html += String(weatherWindDeg);
    html += F("°</p><p><b>Роза ветров:</b></p><pre>");
    html += buildWindRoseAsciiGraph();
    html += F("</pre>");
  }
  html += F("</section>");
  html += F("<p class=\"muted\">NodeMCU ESP-12E NTP + IP geolocation + OpenWeatherMap</p>");
  html += F("</main></body></html>");
  return html;
}

String getDateText(time_t currentTime) {
  struct tm *timeInfo = localtime(&currentTime);
  if (timeInfo == NULL) {
    return F("-- --- ----, -----------");
  }

  String result;
  result.reserve(32);
  result += String(timeInfo->tm_mday);
  result += ' ';
  result += getMonthName(timeInfo->tm_mon);
  result += ' ';
  result += String(timeInfo->tm_year + 1900);
  result += F(", ");
  result += getWeekdayName(timeInfo->tm_wday);
  return result;
}

String getTimeText(time_t currentTime) {
  struct tm *timeInfo = localtime(&currentTime);
  if (timeInfo == NULL) {
    return F("--:--");
  }

  String result;
  result.reserve(8);
  result += twoDigits(timeInfo->tm_hour);
  result += ':';
  result += twoDigits(timeInfo->tm_min);
  return result;
}

String twoDigits(int value) {
  if (value < 10) {
    return "0" + String(value);
  }

  return String(value);
}

const char *getMonthName(int monthIndex) {
  const char *monthNames[] = {
    "января", "февраля", "марта", "апреля", "мая", "июня",
    "июля", "августа", "сентября", "октября", "ноября", "декабря"
  };

  if (monthIndex < 0 || monthIndex > 11) {
    return "---";
  }

  return monthNames[monthIndex];
}

const char *getWeekdayName(int weekdayIndex) {
  const char *weekdayNames[] = {
    "воскресенье", "понедельник", "вторник", "среда",
    "четверг", "пятница", "суббота"
  };

  if (weekdayIndex < 0 || weekdayIndex > 6) {
    return "-----------";
  }

  return weekdayNames[weekdayIndex];
}
