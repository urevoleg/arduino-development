#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <ArduinoJson.h>
#include <EEPROM.h>
#include <string.h>
#include <time.h>

const char WIFI_SSID[] = "rW";
const char WIFI_PASSWORD[] = "redmiRW2018";
const char OPENWEATHER_API_KEY[] = "your_api_key";

const char NTP_SERVER[] = "pool.ntp.org";
const long TIME_OFFSET_SECONDS = 3L * 60L * 60L;

const byte LIGHT_PIN = A0;
const byte STATUS_LED_PIN = 2;
const byte LCD_I2C_ADDRESS = 0x27;
const byte LCD_COLUMNS = 20;
const byte LCD_ROWS = 4;

const unsigned long LIGHT_READ_INTERVAL = 200;
const unsigned long LCD_UPDATE_INTERVAL = 1000;
const unsigned long LCD_PAGE_INTERVAL = 5000;
const unsigned long NTP_UPDATE_INTERVAL = 30UL * 1000UL;
const unsigned long WEATHER_UPDATE_INTERVAL = 5UL * 60UL * 1000UL;
const unsigned long WEATHER_RETRY_INTERVAL = 30UL * 1000UL;
const unsigned long WEATHER_EEPROM_SAVE_INTERVAL = 30UL * 60UL * 1000UL;
const unsigned long OWM_SUCCESS_RATE_WINDOW = 24UL * 60UL * 60UL * 1000UL;
const unsigned long GEOLOCATION_UPDATE_INTERVAL = 6UL * 60UL * 60UL * 1000UL;
const unsigned long WIFI_RECONNECT_INTERVAL = 10UL * 1000UL;
const unsigned long STATUS_LED_BLINK_INTERVAL = 1000UL;
const unsigned long STATUS_LED_ON_DURATION = 100UL;
const size_t EEPROM_STATE_SIZE = 512;
const int EEPROM_WEATHER_STATE_ADDRESS = 0;
const uint32_t EEPROM_WEATHER_STATE_MAGIC = 0x57584D31UL;
const uint16_t EEPROM_WEATHER_STATE_VERSION = 2;

const float DEFAULT_WEATHER_LATITUDE = 55.567586;
const float DEFAULT_WEATHER_LONGITUDE = 38.225004;
const char DEFAULT_WEATHER_CITY[] = "Ramenskoye";
const float LIGHT_FILTER_K = 0.25;
const float PRESSURE_SMOOTHING_K = 0.3;
const unsigned long PRESSURE_HISTORY_WINDOW = 3UL * 60UL * 60UL * 1000UL;
const byte PRESSURE_HISTORY_COUNT = PRESSURE_HISTORY_WINDOW / WEATHER_UPDATE_INTERVAL;
const byte PRESSURE_COMPARE_SAMPLE_OFFSET = 30UL * 60UL * 1000UL / WEATHER_UPDATE_INTERVAL;
const float PRESSURE_FAST_SPEED_THRESHOLD = 1.5;
const float PRESSURE_SLOW_SPEED_THRESHOLD = 0.2;
const byte EEPROM_CITY_LENGTH = 24;
const byte EEPROM_WEATHER_MAIN_LENGTH = 16;
const byte EEPROM_WEATHER_DESCRIPTION_LENGTH = 32;

struct WeatherPersistentState {
  uint32_t magic;
  uint16_t version;
  bool weatherLoaded;
  bool smoothedPressureReady;
  uint32_t lastSuccessfulWeatherEpoch;
  float weatherTemperature;
  float weatherPressureMmHg;
  int weatherHumidity;
  float weatherWindSpeed;
  int weatherWindDeg;
  float smoothedPressureMmHg;
  byte pressureHistoryWriteIndex;
  byte pressureHistoryStoredCount;
  uint32_t owmSuccessCount;
  uint32_t owmErrorCount;
  char weatherCity[EEPROM_CITY_LENGTH];
  char weatherMain[EEPROM_WEATHER_MAIN_LENGTH];
  char weatherDescription[EEPROM_WEATHER_DESCRIPTION_LENGTH];
  float pressureHistory[PRESSURE_HISTORY_COUNT];
  uint32_t checksum;
};

static_assert(sizeof(WeatherPersistentState) <= EEPROM_STATE_SIZE, "WeatherPersistentState does not fit EEPROM_STATE_SIZE");

LiquidCrystal_I2C lcd(LCD_I2C_ADDRESS, LCD_COLUMNS, LCD_ROWS);
WiFiUDP ntpUdp;
NTPClient timeClient(ntpUdp, NTP_SERVER, TIME_OFFSET_SECONDS, NTP_UPDATE_INTERVAL);
StaticJsonDocument<1024> geolocationJsonDoc;
StaticJsonDocument<1536> weatherJsonDoc;

unsigned long lastLightReadMillis = 0;
unsigned long lastLcdUpdateMillis = 0;
unsigned long lastLcdPageMillis = 0;
unsigned long lastWifiReconnectAttempt = 0;
unsigned long lastGeolocationUpdate = 0;
unsigned long lastWeatherUpdate = 0;
unsigned long lastWeatherStateSaveMillis = 0;
time_t lastSuccessfulWeatherEpoch = 0;
unsigned long lastStatusLedBlink = 0;
bool statusLedActive = false;
bool timeWasSynchronized = false;
bool geolocationWasLoaded = false;
bool geolocationLastUpdateOk = false;
bool weatherWasLoaded = false;
bool weatherLastUpdateOk = false;
bool smoothedPressureReady = false;
bool lightFilterReady = false;
byte lcdPage = 0;

int lightRaw = 0;
float lightFiltered = 0.0;
float weatherLatitude = DEFAULT_WEATHER_LATITUDE;
float weatherLongitude = DEFAULT_WEATHER_LONGITUDE;
String weatherCity = DEFAULT_WEATHER_CITY;
String geolocationCity = "";
String geolocationCountry = "";
String geolocationPublicIp = "";
String geolocationStatusText = "wait";
String weatherStatusText = "wait";
float weatherTemperature = 0.0;
float weatherPressureMmHg = 0.0;
int weatherHumidity = 0;
float weatherWindSpeed = 0.0;
int weatherWindDeg = 0;
String weatherMain = "";
String weatherDescription = "";
float smoothedPressureMmHg = 0.0;
float pressureHistory[PRESSURE_HISTORY_COUNT];
byte pressureHistoryWriteIndex = 0;
byte pressureHistoryStoredCount = 0;
uint32_t owmSuccessCount = 0;
uint32_t owmErrorCount = 0;
unsigned long owmSuccessRateWindowStartedMillis = 0;

void connectToWifi();
void reconnectWifiIfNeeded();
void initializePersistentWeatherState();
bool loadWeatherStateFromEeprom();
void saveWeatherStateToEeprom();
void saveWeatherStateToEepromIfDue();
uint32_t calculateWeatherStateChecksum(const WeatherPersistentState &state);
void copyStringToBuffer(const String &source, char *target, size_t targetSize);
void updateStatusLed();
void updateTimeIfNeeded();
void updateGeolocationIfNeeded();
void updateWeatherIfNeeded();
void resetOwmSuccessRateIfNeeded();
void recordOwmRequestResult(bool success);
bool fetchGeolocation();
bool parseGeolocationJson(const String &payload);
bool fetchWeather();
bool parseWeatherJson(const String &payload);
float hpaToMmHg(float pressureHpa);
void addPressureSample(float pressureMmHg);
void initializePressureHistoryIfEmpty(float pressureMmHg);
void storeFilteredPressure(float pressureMmHg);
bool getPressureHistoryByAge(byte ageFromNewest, float *pressureMmHg);
bool getCurrentFilteredPressure(float *pressureMmHg);
bool getPressureSpeed(float *speedMmHgPerHour);
byte getPressureForecastLevel(float speedMmHgPerHour);
const char *getPressureForecastText(byte level);
void initializeLcd();
void readLight();
void updateDisplay();
void showMainPage();
void showPressurePage();
void showStatusPage();
void printLine(byte row, const String &text);
String sanitizeLcdText(const String &text);
String fitText(const String &text, byte width);
String buildGeolocationUrl();
String buildWeatherUrl();
String getDateTimeLine(time_t currentTime);
String getTimeText(time_t currentTime);
String getWeatherUpdateText();
String getOwmSuccessRateText();
String twoDigits(int value);

void setup() {
  pinMode(LIGHT_PIN, INPUT);
  pinMode(STATUS_LED_PIN, OUTPUT);
  digitalWrite(STATUS_LED_PIN, LOW);

  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println(F("Inner meteo module"));

  Wire.begin(4, 5);
  Wire.setClock(100000);
  initializeLcd();
  initializePersistentWeatherState();

  connectToWifi();
  timeClient.begin();
  readLight();
}

void loop() {
  unsigned long now = millis();

  updateStatusLed();
  reconnectWifiIfNeeded();
  updateTimeIfNeeded();
  updateGeolocationIfNeeded();
  updateWeatherIfNeeded();

  if (now - lastLightReadMillis >= LIGHT_READ_INTERVAL) {
    lastLightReadMillis = now;
    readLight();
  }

  if (now - lastLcdPageMillis >= LCD_PAGE_INTERVAL) {
    lastLcdPageMillis = now;
    lcdPage = (lcdPage + 1) % 3;
    lcd.clear();
  }

  if (now - lastLcdUpdateMillis >= LCD_UPDATE_INTERVAL) {
    lastLcdUpdateMillis = now;
    updateDisplay();
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

void initializePersistentWeatherState() {
  EEPROM.begin(EEPROM_STATE_SIZE);
  if (loadWeatherStateFromEeprom()) {
    Serial.println(F("Weather state loaded from EEPROM"));
    weatherStatusText = F("cached");
  } else {
    Serial.println(F("No valid weather state in EEPROM"));
  }
}

bool loadWeatherStateFromEeprom() {
  WeatherPersistentState state;
  EEPROM.get(EEPROM_WEATHER_STATE_ADDRESS, state);

  uint32_t storedChecksum = state.checksum;
  state.checksum = 0;
  if (state.magic != EEPROM_WEATHER_STATE_MAGIC ||
      state.version != EEPROM_WEATHER_STATE_VERSION ||
      storedChecksum != calculateWeatherStateChecksum(state)) {
    return false;
  }

  if (state.pressureHistoryStoredCount > PRESSURE_HISTORY_COUNT ||
      state.pressureHistoryWriteIndex >= PRESSURE_HISTORY_COUNT) {
    return false;
  }

  weatherWasLoaded = state.weatherLoaded;
  smoothedPressureReady = state.smoothedPressureReady;
  lastSuccessfulWeatherEpoch = static_cast<time_t>(state.lastSuccessfulWeatherEpoch);
  weatherTemperature = state.weatherTemperature;
  weatherPressureMmHg = state.weatherPressureMmHg;
  weatherHumidity = state.weatherHumidity;
  weatherWindSpeed = state.weatherWindSpeed;
  weatherWindDeg = state.weatherWindDeg;
  smoothedPressureMmHg = state.smoothedPressureMmHg;
  pressureHistoryWriteIndex = state.pressureHistoryWriteIndex;
  pressureHistoryStoredCount = state.pressureHistoryStoredCount;
  owmSuccessCount = state.owmSuccessCount;
  owmErrorCount = state.owmErrorCount;
  weatherCity = state.weatherCity;
  weatherMain = state.weatherMain;
  weatherDescription = state.weatherDescription;

  for (byte i = 0; i < PRESSURE_HISTORY_COUNT; i++) {
    pressureHistory[i] = state.pressureHistory[i];
  }

  return weatherWasLoaded;
}

void saveWeatherStateToEeprom() {
  WeatherPersistentState state;
  memset(&state, 0, sizeof(state));

  state.magic = EEPROM_WEATHER_STATE_MAGIC;
  state.version = EEPROM_WEATHER_STATE_VERSION;
  state.weatherLoaded = weatherWasLoaded;
  state.smoothedPressureReady = smoothedPressureReady;
  state.lastSuccessfulWeatherEpoch = static_cast<uint32_t>(lastSuccessfulWeatherEpoch);
  state.weatherTemperature = weatherTemperature;
  state.weatherPressureMmHg = weatherPressureMmHg;
  state.weatherHumidity = weatherHumidity;
  state.weatherWindSpeed = weatherWindSpeed;
  state.weatherWindDeg = weatherWindDeg;
  state.smoothedPressureMmHg = smoothedPressureMmHg;
  state.pressureHistoryWriteIndex = pressureHistoryWriteIndex;
  state.pressureHistoryStoredCount = pressureHistoryStoredCount;
  state.owmSuccessCount = owmSuccessCount;
  state.owmErrorCount = owmErrorCount;
  copyStringToBuffer(weatherCity, state.weatherCity, sizeof(state.weatherCity));
  copyStringToBuffer(weatherMain, state.weatherMain, sizeof(state.weatherMain));
  copyStringToBuffer(weatherDescription, state.weatherDescription, sizeof(state.weatherDescription));

  for (byte i = 0; i < PRESSURE_HISTORY_COUNT; i++) {
    state.pressureHistory[i] = pressureHistory[i];
  }

  state.checksum = calculateWeatherStateChecksum(state);
  EEPROM.put(EEPROM_WEATHER_STATE_ADDRESS, state);
  if (EEPROM.commit()) {
    Serial.println(F("Weather state saved to EEPROM"));
  } else {
    Serial.println(F("EEPROM weather save failed"));
  }
}

void saveWeatherStateToEepromIfDue() {
  unsigned long now = millis();
  if (lastWeatherStateSaveMillis != 0 &&
      now - lastWeatherStateSaveMillis < WEATHER_EEPROM_SAVE_INTERVAL) {
    return;
  }

  saveWeatherStateToEeprom();
  lastWeatherStateSaveMillis = now;
}

uint32_t calculateWeatherStateChecksum(const WeatherPersistentState &state) {
  const uint8_t *bytes = reinterpret_cast<const uint8_t *>(&state);
  uint32_t checksum = 2166136261UL;

  for (size_t i = 0; i < sizeof(state); i++) {
    checksum ^= bytes[i];
    checksum *= 16777619UL;
  }

  return checksum;
}

void copyStringToBuffer(const String &source, char *target, size_t targetSize) {
  if (targetSize == 0) {
    return;
  }

  strncpy(target, source.c_str(), targetSize - 1);
  target[targetSize - 1] = '\0';
}

void resetOwmSuccessRateIfNeeded() {
  unsigned long now = millis();
  if (owmSuccessRateWindowStartedMillis == 0) {
    owmSuccessRateWindowStartedMillis = now;
    return;
  }

  if (now - owmSuccessRateWindowStartedMillis >= OWM_SUCCESS_RATE_WINDOW) {
    owmSuccessCount = 0;
    owmErrorCount = 0;
    owmSuccessRateWindowStartedMillis = now;
  }
}

void recordOwmRequestResult(bool success) {
  resetOwmSuccessRateIfNeeded();
  if (success) {
    owmSuccessCount++;
  } else {
    owmErrorCount++;
  }
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

  if (timeClient.update()) {
    timeWasSynchronized = true;
  }
}

void updateGeolocationIfNeeded() {
  if (WiFi.status() != WL_CONNECTED) {
    geolocationLastUpdateOk = false;
    geolocationStatusText = F("WiFi down");
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

void updateWeatherIfNeeded() {
  if (WiFi.status() != WL_CONNECTED) {
    weatherLastUpdateOk = false;
    weatherStatusText = F("WiFi down");
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
    if (timeWasSynchronized) {
      lastSuccessfulWeatherEpoch = timeClient.getEpochTime();
    }
    saveWeatherStateToEepromIfDue();
  }
}

bool fetchGeolocation() {
  WiFiClient client;
  HTTPClient http;
  String url = buildGeolocationUrl();

  if (!http.begin(client, url)) {
    geolocationStatusText = F("HTTP init");
    return false;
  }

  int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    geolocationStatusText = F("HTTP ");
    geolocationStatusText += String(httpCode);
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
    geolocationStatusText = F("JSON geo");
    return false;
  }

  const char *status = geolocationJsonDoc["status"] | "";
  if (strcmp(status, "success") != 0) {
    geolocationStatusText = geolocationJsonDoc["message"] | "geo failed";
    return false;
  }

  weatherLatitude = geolocationJsonDoc["lat"] | DEFAULT_WEATHER_LATITUDE;
  weatherLongitude = geolocationJsonDoc["lon"] | DEFAULT_WEATHER_LONGITUDE;
  geolocationCity = geolocationJsonDoc["city"] | "";
  geolocationCountry = geolocationJsonDoc["country"] | "";
  geolocationPublicIp = geolocationJsonDoc["query"] | "";
  if (!weatherWasLoaded) {
    weatherCity = geolocationCity.length() > 0 ? geolocationCity : DEFAULT_WEATHER_CITY;
  }

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
    weatherStatusText = F("HTTP init");
    recordOwmRequestResult(false);
    return false;
  }

  int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    weatherStatusText = F("HTTP ");
    weatherStatusText += String(httpCode);
    http.end();
    recordOwmRequestResult(false);
    return false;
  }

  String payload = http.getString();
  http.end();
  bool parsed = parseWeatherJson(payload);
  recordOwmRequestResult(parsed);
  return parsed;
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
  url += F("&units=metric&lang=en");
  return url;
}

bool parseWeatherJson(const String &payload) {
  weatherJsonDoc.clear();
  DeserializationError error = deserializeJson(weatherJsonDoc, payload);
  if (error) {
    weatherStatusText = F("JSON weather");
    return false;
  }

  weatherTemperature = weatherJsonDoc["main"]["temp"] | 0.0;
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

  addPressureSample(weatherPressureMmHg);
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

void initializePressureHistoryIfEmpty(float pressureMmHg) {
  if (pressureHistoryStoredCount > 0) {
    return;
  }

  for (byte i = 0; i < PRESSURE_HISTORY_COUNT; i++) {
    pressureHistory[i] = pressureMmHg;
  }
  pressureHistoryWriteIndex = 0;
  pressureHistoryStoredCount = PRESSURE_HISTORY_COUNT;
}

void storeFilteredPressure(float pressureMmHg) {
  pressureHistory[pressureHistoryWriteIndex] = pressureMmHg;
  pressureHistoryWriteIndex = (pressureHistoryWriteIndex + 1) % PRESSURE_HISTORY_COUNT;

  if (pressureHistoryStoredCount < PRESSURE_HISTORY_COUNT) {
    pressureHistoryStoredCount++;
  }
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
  if (!getPressureHistoryByAge(PRESSURE_COMPARE_SAMPLE_OFFSET, &previousPressure)) {
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

const char *getPressureForecastText(byte level) {
  const char *texts[] = { "fast fall", "fall", "stable", "rise", "fast rise" };
  if (level > 4) {
    return "no data";
  }

  return texts[level];
}

void initializeLcd() {
  Serial.println(F("LCD init started"));
  lcd.init();
  lcd.backlight();
  printLine(0, F("Inner meteo module"));
  printLine(1, F("LCD2004 ready"));
  printLine(2, F("WiFi + weather"));
  printLine(3, F("Starting..."));
  Serial.println(F("LCD init done"));
  delay(2000);
  lcd.clear();
}

void readLight() {
  lightRaw = analogRead(LIGHT_PIN);
  if (lightFilterReady) {
    lightFiltered = LIGHT_FILTER_K * lightRaw + (1.0 - LIGHT_FILTER_K) * lightFiltered;
  } else {
    lightFiltered = lightRaw;
    lightFilterReady = true;
  }

  Serial.print(F("Light raw: "));
  Serial.print(lightRaw);
  Serial.print(F(", filtered: "));
  Serial.println(lightFiltered, 1);
}

void updateDisplay() {
  resetOwmSuccessRateIfNeeded();

  switch (lcdPage) {
    case 0:
      showMainPage();
      break;
    case 1:
      showPressurePage();
      break;
    default:
      showStatusPage();
      break;
  }
}

void showMainPage() {
  time_t currentTime = timeClient.getEpochTime();

  if (timeWasSynchronized) {
    printLine(0, getDateTimeLine(currentTime));
  } else {
    printLine(0, F("--:-- Time sync wait"));
  }

  if (!weatherWasLoaded) {
    printLine(1, F("Weather: no data"));
    printLine(2, "OWM: " + weatherStatusText);
    printLine(3, F("Waiting update"));
    return;
  }

  printLine(1, "Temp " + String(weatherTemperature, 1) + "C");
  printLine(2, "Hum " + String(weatherHumidity) + "% Wind " + String(weatherWindSpeed, 1) + "m/s");
  printLine(3, fitText(weatherMain + " " + weatherDescription, LCD_COLUMNS));
}

void showPressurePage() {
  float filteredPressureMmHg = 0.0;
  float pressureSpeedMmHgPerHour = 0.0;
  bool hasFilteredPressure = getCurrentFilteredPressure(&filteredPressureMmHg);
  bool hasPressureSpeed = getPressureSpeed(&pressureSpeedMmHgPerHour);
  byte forecastLevel = hasPressureSpeed ? getPressureForecastLevel(pressureSpeedMmHgPerHour) : 2;

  if (!weatherWasLoaded) {
    printLine(0, F("Pressure: no data"));
    printLine(1, "OWM: " + weatherStatusText);
    printLine(2, F(""));
    printLine(3, F(""));
    return;
  }

  printLine(0, "P " + String(weatherPressureMmHg, 1) + " mmHg");
  if (hasFilteredPressure) {
    printLine(1, "EMA " + String(filteredPressureMmHg, 1) + " mmHg");
  } else {
    printLine(1, F("EMA no data"));
  }
  if (hasPressureSpeed) {
    printLine(2, "Speed " + String(pressureSpeedMmHgPerHour, 1) + " mm/h");
  } else {
    printLine(2, F("Speed collecting"));
  }
  printLine(3, "Fcst " + String(getPressureForecastText(forecastLevel)));
}

void showStatusPage() {
  printLine(0, "Geo:" + geolocationStatusText + " OWM:" + weatherStatusText);
  if (geolocationWasLoaded) {
    printLine(1, geolocationCity + " " + geolocationCountry);
  } else {
    printLine(1, F("Geo no data"));
  }
  printLine(2, getWeatherUpdateText());
  printLine(3, getOwmSuccessRateText());
}

void printLine(byte row, const String &text) {
  lcd.setCursor(0, row);
  lcd.print(fitText(text, LCD_COLUMNS));
}

String fitText(const String &text, byte width) {
  String result = sanitizeLcdText(text);
  if (result.length() > width) {
    result.remove(width);
  }
  while (result.length() < width) {
    result += ' ';
  }
  return result;
}

String sanitizeLcdText(const String &text) {
  String result;
  result.reserve(text.length());

  for (unsigned int i = 0; i < text.length(); i++) {
    unsigned char value = static_cast<unsigned char>(text[i]);
    if (value >= 32 && value <= 126) {
      result += static_cast<char>(value);
    } else if (value < 128) {
      result += ' ';
    } else {
      result += '?';
    }
  }

  return result;
}

String getDateTimeLine(time_t currentTime) {
  struct tm *timeInfo = localtime(&currentTime);
  if (timeInfo == NULL) {
    return F("--:-- -- --- ----");
  }

  const char *months[] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
  };
  const char *weekdays[] = {
    "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
  };

  return getTimeText(currentTime) + " " +
         String(timeInfo->tm_mday) +
         months[timeInfo->tm_mon] +
         String(timeInfo->tm_year + 1900) + " " +
         weekdays[timeInfo->tm_wday];
}

String getTimeText(time_t currentTime) {
  struct tm *timeInfo = localtime(&currentTime);
  if (timeInfo == NULL) {
    return F("--:--");
  }

  return twoDigits(timeInfo->tm_hour) + ":" + twoDigits(timeInfo->tm_min);
}

String getWeatherUpdateText() {
  if (lastSuccessfulWeatherEpoch == 0) {
    return F("Weather upd: none");
  }

  return "Weather upd " + getTimeText(lastSuccessfulWeatherEpoch);
}

String getOwmSuccessRateText() {
  uint32_t total = owmSuccessCount + owmErrorCount;
  if (total == 0) {
    return F("OWM SR no data");
  }

  uint32_t successRate = (owmSuccessCount * 100UL + total / 2) / total;
  return "OWM SR " + String(successRate) + "% " +
         String(owmSuccessCount) + "/" + String(total);
}

String twoDigits(int value) {
  if (value < 10) {
    return "0" + String(value);
  }

  return String(value);
}
