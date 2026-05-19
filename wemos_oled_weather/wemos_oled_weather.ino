#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ArduinoJson.h>

// Имя WiFi-сети; строка, без единиц измерения.
const char WIFI_SSID[] = "YOUR_WIFI_SSID";
// Пароль WiFi-сети; строка, без единиц измерения.
const char WIFI_PASSWORD[] = "YOUR_WIFI_PASSWORD";
// Ключ OpenWeatherMap API; строка, без единиц измерения.
const char OPENWEATHER_API_KEY[] = "YOUR_OPENWEATHER_API_KEY";

// Ширина OLED-дисплея; пиксели.
const int SCREEN_WIDTH = 128;
// Высота OLED-дисплея; пиксели.
const int SCREEN_HEIGHT = 64;
// I2C-адрес SSD1306 OLED-дисплея; без единиц измерения.
const byte OLED_I2C_ADDRESS = 0x3C;
// Пин SDA шины I2C; номер GPIO, GPIO4 = D2 на Wemos D1 mini.
const byte I2C_SDA_PIN = 4;
// Пин SCL шины I2C; номер GPIO, GPIO5 = D1 на Wemos D1 mini.
const byte I2C_SCL_PIN = 5;

// Интервал загрузки погоды из OpenWeatherMap; миллисекунды.
const unsigned long WEATHER_UPDATE_INTERVAL = 5UL * 60UL * 1000UL;
// Интервал попыток переподключения к WiFi; миллисекунды.
const unsigned long WIFI_RECONNECT_INTERVAL = 10UL * 1000UL;
// Интервал перерисовки OLED; миллисекунды.
const unsigned long DISPLAY_UPDATE_INTERVAL = 1000UL;
// Интервал смены страницы OLED; миллисекунды.
const unsigned long DISPLAY_PAGE_INTERVAL = 4000UL;
// Количество страниц OLED; штуки.
const byte DISPLAY_PAGE_COUNT = 3;

// Длительность окна усреднения температуры и давления; миллисекунды.
const unsigned long AVERAGE_WINDOW = 15UL * 60UL * 1000UL;
// Количество выборок в 15-минутном окне при обновлении раз в 5 минут; штуки.
const byte AVERAGE_SAMPLE_COUNT = AVERAGE_WINDOW / WEATHER_UPDATE_INTERVAL;

// Широта точки прогноза погоды; градусы.
const float WEATHER_LATITUDE = 55.567586;
// Долгота точки прогноза погоды; градусы.
const float WEATHER_LONGITUDE = 38.225004;
// Название места для отображения; строка, без единиц измерения.
const char WEATHER_PLACE[] = "Ramenskoye";

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

unsigned long lastWifiReconnectAttempt = 0;
unsigned long lastWeatherUpdate = 0;
unsigned long lastDisplayUpdate = 0;
unsigned long lastPageChange = 0;
byte displayPage = 0;
bool displayReady = false;
bool weatherWasLoaded = false;
bool weatherLastUpdateOk = false;

float weatherTemperature = 0.0;
float weatherPressureMmHg = 0.0;
float weatherWindSpeed = 0.0;
int weatherHumidity = 0;
int weatherWindDeg = 0;
String weatherDescription = "";
String weatherStatusText = "Waiting weather";

float temperatureSamples[AVERAGE_SAMPLE_COUNT];
float pressureSamples[AVERAGE_SAMPLE_COUNT];
byte averageWriteIndex = 0;
byte averageStoredCount = 0;

void connectToWifi();
void reconnectWifiIfNeeded();
void initializeDisplay();
void updateWeatherIfNeeded();
bool fetchWeather();
String buildWeatherUrl();
bool parseWeatherJson(const String &payload);
float hpaToMmHg(float pressureHpa);
void addAverageSample(float temperatureC, float pressureMmHg);
bool getAverageTemperature(float *temperatureC);
bool getAveragePressure(float *pressureMmHg);
void updateDisplayIfNeeded();
void drawCurrentWeatherPage();
void drawAveragePage();
void drawWindPage();
void drawHeader(const char *title);
void drawStatusLine();
void printValueOrDash(float value, byte decimals, const char *unit, bool available);
const char *getWindDirectionText(int degrees);
String trimToDisplay(String value, byte maxChars);

void setup() {
  Serial.begin(115200);
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);

  initializeDisplay();
  connectToWifi();

  if (displayReady) {
    display.clearDisplay();
    drawHeader("Weather");
    display.setCursor(0, 18);
    display.println(F("Connecting WiFi"));
    display.display();
  }

  lastWeatherUpdate = millis() - WEATHER_UPDATE_INTERVAL;
}

void loop() {
  reconnectWifiIfNeeded();
  updateWeatherIfNeeded();
  updateDisplayIfNeeded();
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
  weatherLastUpdateOk = false;
  weatherStatusText = F("WiFi offline");
  WiFi.disconnect();
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.println(F("WiFi reconnecting"));
}

void initializeDisplay() {
  displayReady = display.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDRESS);
  if (!displayReady) {
    Serial.println(F("OLED error: SSD1306 not found"));
    return;
  }

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.display();
}

void updateWeatherIfNeeded() {
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  unsigned long now = millis();
  if (lastWeatherUpdate != 0 && now - lastWeatherUpdate < WEATHER_UPDATE_INTERVAL) {
    return;
  }

  lastWeatherUpdate = now;
  weatherLastUpdateOk = fetchWeather();
  if (weatherLastUpdateOk) {
    weatherWasLoaded = true;
    weatherStatusText = F("API OK");
  }
}

bool fetchWeather() {
  WiFiClient client;
  HTTPClient http;
  String url = buildWeatherUrl();

  if (!http.begin(client, url)) {
    weatherStatusText = F("HTTP init err");
    return false;
  }

  int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    weatherStatusText = F("HTTP ");
    weatherStatusText += String(httpCode);
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
  url += String(WEATHER_LATITUDE, 6);
  url += F("&lon=");
  url += String(WEATHER_LONGITUDE, 6);
  url += F("&appid=");
  url += OPENWEATHER_API_KEY;
  url += F("&units=metric&lang=en");
  return url;
}

bool parseWeatherJson(const String &payload) {
  StaticJsonDocument<1536> doc;
  DeserializationError error = deserializeJson(doc, payload);
  if (error) {
    weatherStatusText = F("JSON error");
    return false;
  }

  weatherTemperature = doc["main"]["temp"] | 0.0;
  weatherHumidity = doc["main"]["humidity"] | 0;
  weatherPressureMmHg = hpaToMmHg(doc["main"]["pressure"] | 0.0);
  weatherWindSpeed = doc["wind"]["speed"] | 0.0;
  weatherWindDeg = doc["wind"]["deg"] | 0;
  weatherDescription = doc["weather"][0]["description"] | "";
  addAverageSample(weatherTemperature, weatherPressureMmHg);

  Serial.print(F("Weather OK: "));
  Serial.print(weatherTemperature, 1);
  Serial.print(F(" C, "));
  Serial.print(weatherHumidity);
  Serial.print(F("%, "));
  Serial.print(weatherPressureMmHg, 1);
  Serial.println(F(" mmHg"));
  return true;
}

float hpaToMmHg(float pressureHpa) {
  return pressureHpa * 0.750062;
}

void addAverageSample(float temperatureC, float pressureMmHg) {
  temperatureSamples[averageWriteIndex] = temperatureC;
  pressureSamples[averageWriteIndex] = pressureMmHg;
  averageWriteIndex = (averageWriteIndex + 1) % AVERAGE_SAMPLE_COUNT;

  if (averageStoredCount < AVERAGE_SAMPLE_COUNT) {
    averageStoredCount++;
  }
}

bool getAverageTemperature(float *temperatureC) {
  if (averageStoredCount == 0) {
    return false;
  }

  float sum = 0.0;
  for (byte i = 0; i < averageStoredCount; i++) {
    sum += temperatureSamples[i];
  }
  *temperatureC = sum / averageStoredCount;
  return true;
}

bool getAveragePressure(float *pressureMmHg) {
  if (averageStoredCount == 0) {
    return false;
  }

  float sum = 0.0;
  for (byte i = 0; i < averageStoredCount; i++) {
    sum += pressureSamples[i];
  }
  *pressureMmHg = sum / averageStoredCount;
  return true;
}

void updateDisplayIfNeeded() {
  if (!displayReady) {
    return;
  }

  unsigned long now = millis();
  if (now - lastPageChange >= DISPLAY_PAGE_INTERVAL) {
    lastPageChange = now;
    displayPage = (displayPage + 1) % DISPLAY_PAGE_COUNT;
  }
  if (now - lastDisplayUpdate < DISPLAY_UPDATE_INTERVAL) {
    return;
  }

  lastDisplayUpdate = now;
  display.clearDisplay();

  if (displayPage == 0) {
    drawCurrentWeatherPage();
  } else if (displayPage == 1) {
    drawAveragePage();
  } else {
    drawWindPage();
  }

  display.display();
}

void drawCurrentWeatherPage() {
  drawHeader(WEATHER_PLACE);
  display.setCursor(0, 14);

  if (!weatherWasLoaded) {
    display.println(F("No weather data"));
    drawStatusLine();
    return;
  }

  display.print(F("Temp: "));
  display.print(weatherTemperature, 1);
  display.println(F(" C"));
  display.print(F("Hum : "));
  display.print(weatherHumidity);
  display.println(F(" %"));
  display.print(F("Pres: "));
  display.print(weatherPressureMmHg, 1);
  display.println(F(" mm"));
  drawStatusLine();
}

void drawAveragePage() {
  float averageTemperature = 0.0;
  float averagePressure = 0.0;
  bool hasTemperature = getAverageTemperature(&averageTemperature);
  bool hasPressure = getAveragePressure(&averagePressure);

  drawHeader("15 min avg");
  display.setCursor(0, 16);
  display.print(F("Temp: "));
  printValueOrDash(averageTemperature, 1, " C", hasTemperature);
  display.println();
  display.print(F("Pres: "));
  printValueOrDash(averagePressure, 1, " mm", hasPressure);
  display.println();
  display.print(F("Samples: "));
  display.print(averageStoredCount);
  display.print(F("/"));
  display.println(AVERAGE_SAMPLE_COUNT);
  drawStatusLine();
}

void drawWindPage() {
  drawHeader("Wind");
  display.setCursor(0, 14);

  if (!weatherWasLoaded) {
    display.println(F("No weather data"));
    drawStatusLine();
    return;
  }

  display.print(F("Speed: "));
  display.print(weatherWindSpeed, 1);
  display.println(F(" m/s"));
  display.print(F("Dir  : "));
  display.print(weatherWindDeg);
  display.print(F(" "));
  display.println(getWindDirectionText(weatherWindDeg));
  display.print(F("Desc : "));
  display.println(trimToDisplay(weatherDescription, 14));
  drawStatusLine();
}

void drawHeader(const char *title) {
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print(title);
  display.drawLine(0, 10, SCREEN_WIDTH - 1, 10, SSD1306_WHITE);
}

void drawStatusLine() {
  display.drawLine(0, 54, SCREEN_WIDTH - 1, 54, SSD1306_WHITE);
  display.setCursor(0, 56);
  display.print(weatherLastUpdateOk ? F("OK ") : F("ERR "));
  display.print(trimToDisplay(weatherStatusText, 17));
}

void printValueOrDash(float value, byte decimals, const char *unit, bool available) {
  if (!available) {
    display.print(F("--"));
    return;
  }

  display.print(value, decimals);
  display.print(unit);
}

const char *getWindDirectionText(int degrees) {
  int normalized = degrees % 360;
  if (normalized < 0) {
    normalized += 360;
  }

  if (normalized >= 338 || normalized < 23) {
    return "N";
  }
  if (normalized < 68) {
    return "NE";
  }
  if (normalized < 113) {
    return "E";
  }
  if (normalized < 158) {
    return "SE";
  }
  if (normalized < 203) {
    return "S";
  }
  if (normalized < 248) {
    return "SW";
  }
  if (normalized < 293) {
    return "W";
  }
  return "NW";
}

String trimToDisplay(String value, byte maxChars) {
  value.trim();
  if (value.length() <= maxChars) {
    return value;
  }

  return value.substring(0, maxChars);
}
