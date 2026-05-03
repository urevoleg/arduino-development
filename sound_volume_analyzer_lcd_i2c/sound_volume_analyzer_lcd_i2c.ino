#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// Настройки подключения собраны здесь, чтобы не менять логику при смене пинов.
const byte SOUND_SENSOR_PIN = A0;
const byte STATUS_LED_PIN = 13;
const byte LCD_I2C_ADDRESS = 0x27;    // Если дисплей не найден, попробуйте 0x3F.
const byte LCD_COLUMNS = 16;
const byte LCD_ROWS = 2;

// 10 измерений по 100 мс дают окно скользящего среднего ровно 1 секунду.
const unsigned long SENSOR_READ_INTERVAL = 100;
const byte AVERAGE_SAMPLE_COUNT = 10;
const unsigned long LCD_CHECK_INTERVAL = 5000;
const unsigned long ACTIVITY_LED_DURATION = 20;
const unsigned long CALIBRATION_DURATION = 1000;
const int NOISE_REDUCTION = 5;

const int ADC_MIN_VALUE = 0;
const int ADC_MAX_VALUE = 1023;
const int MAX_VOLUME_LEVEL = ADC_MAX_VALUE;

LiquidCrystal_I2C lcd(LCD_I2C_ADDRESS, LCD_COLUMNS, LCD_ROWS);

unsigned long lastSensorReadMillis = 0;
unsigned long lastLcdCheckMillis = 0;
unsigned long activityLedStartedAt = 0;
bool lcdConnected = false;
bool activityLedActive = false;
int soundBaseline = 0;

void initializeDisplay();
bool isI2cDeviceConnected(byte address);
void checkLcdConnection();
int calibrateSoundBaseline();
int readSoundLevel();
int addSoundLevelSample(int rawLevel);
int reduceNoise(int averageLevel);
byte convertVolumeToPercent(int volumeLevel);
void loadLevelBarChars();
void showVolume(int volumeLevel, byte volumePercent);
void showMessage(const char *line1, const char *line2);
void blinkActivity();
void updateActivityLed(unsigned long now);

void setup() {
  pinMode(STATUS_LED_PIN, OUTPUT);
  pinMode(SOUND_SENSOR_PIN, INPUT);

  // Serial нужен как запасной вывод, если LCD не подключен или отключился.
  Serial.begin(9600);
  Wire.begin();

  initializeDisplay();

  if (lcdConnected) {
    showMessage("Calibrating", "keep silence");
  }

  soundBaseline = calibrateSoundBaseline();

  if (lcdConnected) {
    showMessage("Sound analyzer", "A0 calibrated");
  }

  Serial.println(F("Sound volume analyzer started"));
  Serial.print(F("Sound baseline: "));
  Serial.println(soundBaseline);
  if (!lcdConnected) {
    Serial.println(F("LCD error: display not found on I2C"));
  }
}

void loop() {
  unsigned long now = millis();
  updateActivityLed(now);

  if (now - lastLcdCheckMillis >= LCD_CHECK_INTERVAL) {
    lastLcdCheckMillis = now;
    checkLcdConnection();
  }

  if (now - lastSensorReadMillis >= SENSOR_READ_INTERVAL) {
    lastSensorReadMillis = now;

    int rawLevel = readSoundLevel();
    int averageLevel = addSoundLevelSample(rawLevel);
    int volumeLevel = reduceNoise(averageLevel);
    byte volumePercent = convertVolumeToPercent(volumeLevel);

    showVolume(volumeLevel, volumePercent);
    blinkActivity();

    Serial.print(F("Sound raw: "));
    Serial.print(rawLevel);
    Serial.print(F(", avg 1s: "));
    Serial.print(averageLevel);
    Serial.print(F(", baseline: "));
    Serial.print(soundBaseline);
    Serial.print(F(", volume: "));
    Serial.print(volumeLevel);
    Serial.print(F(", level: "));
    Serial.print(volumePercent);
    Serial.println(F("%"));
  }
}
