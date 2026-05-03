#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Adafruit_BMP085.h>

// Настройки подключения собраны здесь, чтобы не менять логику при смене пинов.
const byte DS18B20_DATA_PIN = 3;
const byte STATUS_LED_PIN = 13;
const byte LCD_I2C_ADDRESS = 0x27;    // Если дисплей не найден, попробуйте 0x3F.
const byte BMP180_I2C_ADDRESS = 0x77;
const byte LCD_COLUMNS = 16;
const byte LCD_ROWS = 2;

// Датчики читаются раз в 30 секунд, средние значения считаются за 5 минут.
const unsigned long READ_INTERVAL = 30UL * 1000UL;
const unsigned long LCD_CHECK_INTERVAL = 5000;
const unsigned long ACTIVITY_LED_DURATION = 80;
const unsigned long ERROR_BLINK_INTERVAL = 120;
const byte ERROR_BLINK_TOGGLES = 6;
const unsigned long AVERAGE_WINDOW = 5UL * 60UL * 1000UL;
const byte TEMPERATURE_GRAPH_COLUMNS = 13;
const int16_t TEMPERATURE_INVALID = -32768;
const float TEMPERATURE_DISPLAY_INVALID = -999.0;
const uint16_t AVERAGE_SAMPLE_COUNT = AVERAGE_WINDOW / READ_INTERVAL;
const unsigned long PRESSURE_WINDOW = 5UL * 60UL * 1000UL;
const uint16_t PRESSURE_WINDOW_SAMPLE_COUNT = PRESSURE_WINDOW / READ_INTERVAL;
const unsigned long PRESSURE_HISTORY_WINDOW = 3UL * 60UL * 60UL * 1000UL;
const byte PRESSURE_HISTORY_WINDOW_COUNT = PRESSURE_HISTORY_WINDOW / PRESSURE_WINDOW;
const float PRESSURE_STABLE_SPEED_THRESHOLD = 0.2;
const int16_t PRESSURE_INVALID = -32768;

LiquidCrystal_I2C lcd(LCD_I2C_ADDRESS, LCD_COLUMNS, LCD_ROWS);
OneWire oneWire(DS18B20_DATA_PIN);
DallasTemperature sensors(&oneWire);
Adafruit_BMP085 bmp180;

unsigned long lastReadMillis = 0;
unsigned long lastLcdCheckMillis = 0;
bool lcdConnected = false;
bool sensorConnected = false;
bool bmp180Connected = false;
bool activityLedActive = false;
bool errorBlinkActive = false;
bool errorBlinkLedState = false;
byte errorBlinkTogglesRemaining = 0;
unsigned long activityLedStartedAt = 0;
unsigned long errorBlinkLastToggle = 0;

void initializeSensor();
bool readTemperatureC(float *temperatureC);
void initializePressureSensor();
bool readPressureHpa(float *pressureHpa);
float pressureHpaToMmHg(float pressureHpa);
void initializeDisplay();
bool isI2cDeviceConnected(byte address);
void checkLcdConnection();
void addTemperatureSample(float temperatureC);
bool hasAverageTemperature();
bool isAverageWindowFilled();
int16_t getAverageTemperatureRaw();
uint16_t getTemperatureSampleCount();
int16_t getTemperatureSampleByAge(uint16_t ageFromOldest);
float rawTemperatureToC(int16_t rawTemperature);
void addPressureSample(float pressureMmHg);
bool hasAveragePressure();
int16_t getAveragePressureRaw();
float rawPressureToMmHg(int16_t rawPressure);
byte getStoredPressureWindowCount();
int16_t getStoredPressureWindowByAge(byte ageFromOldest);
char getPressureTrendSymbol();
float getPressureChangeSpeedMmHgPerHour();
void showMeasurements(float temperatureC, float averageTemperatureC, float pressureMmHg);
void loadTemperatureGraphChars();
void drawTemperatureGraphLine(float temperatureC);
void printPressureSpeed(float pressureSpeed);
void printTemperatureInt(float temperatureC);
void showError(const char *line1, const char *line2);
void showMessage(const char *line1, const char *line2);
void blinkActivity();
void blinkError();
void updateStatusLed(unsigned long now);

void setup() {
  pinMode(STATUS_LED_PIN, OUTPUT);

  // Serial нужен как запасной вывод, если LCD не подключен или отключился.
  Serial.begin(9600);
  Wire.begin();

  initializeSensor();
  initializePressureSensor();
  initializeDisplay();

  if (lcdConnected) {
    showMessage("Weather monitor", "Starting...");
  }

  Serial.println(F("Weather monitor started"));
  if (!lcdConnected) {
    Serial.println(F("LCD error: display not found on I2C"));
  }
  if (!sensorConnected) {
    Serial.println(F("Sensor error: DS18B20 not found"));
  }
  if (!bmp180Connected) {
    Serial.println(F("Sensor error: BMP180 not found"));
  }

  lastReadMillis = millis() - READ_INTERVAL;
}

void loop() {
  unsigned long now = millis();
  updateStatusLed(now);

  if (now - lastLcdCheckMillis >= LCD_CHECK_INTERVAL) {
    lastLcdCheckMillis = now;
    checkLcdConnection();
  }

  if (now - lastReadMillis >= READ_INTERVAL) {
    lastReadMillis = now;
    readAndShowTemperature();
  }
}

void readAndShowTemperature() {
  blinkActivity();

  float temperatureC = 0.0;
  bool temperatureOk = readTemperatureC(&temperatureC);
  if (!temperatureOk) {
    blinkError();
  }

  float pressureHpa = 0.0;
  bool pressureOk = readPressureHpa(&pressureHpa);
  float pressureMmHg = pressureOk ? pressureHpaToMmHg(pressureHpa) : -1.0;

  if (temperatureOk) {
    addTemperatureSample(temperatureC);
  }
  if (pressureOk) {
    addPressureSample(pressureMmHg);
  }

  float averageTemperatureC = temperatureOk ? rawTemperatureToC(getAverageTemperatureRaw()) : TEMPERATURE_DISPLAY_INVALID;
  float averagePressureMmHg = pressureOk ? rawPressureToMmHg(getAveragePressureRaw()) : -1.0;
  showMeasurements(
    temperatureOk ? temperatureC : TEMPERATURE_DISPLAY_INVALID,
    averageTemperatureC,
    averagePressureMmHg
  );

  if (temperatureOk) {
    Serial.print(F("Temperature: "));
    Serial.print(temperatureC, 1);
    Serial.print(F(" C, avg 5 min: "));
    Serial.print(averageTemperatureC, 1);
    Serial.print(F(" C"));
  } else {
    Serial.print(F("Temperature: DS18B20 error"));
  }
  if (pressureOk) {
    Serial.print(F(", pressure: "));
    Serial.print(averagePressureMmHg, 1);
    Serial.print(F(" mmHg avg 5 min, trend: "));
    Serial.print(getPressureTrendSymbol());
    Serial.print(F(", speed: "));
    Serial.print(getPressureChangeSpeedMmHgPerHour(), 1);
    Serial.print(F(" mmHg/h"));
  } else {
    Serial.print(F(", pressure: BMP180 error"));
  }
  Serial.println();
}
