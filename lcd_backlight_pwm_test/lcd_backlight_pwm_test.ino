#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

const byte LCD_BACKLIGHT_PIN = 0;  // D3/GPIO0.
const byte LCD_I2C_ADDRESS = 0x27;
const byte LCD_COLUMNS = 20;
const byte LCD_ROWS = 4;
const unsigned long BLINK_INTERVAL_MS = 1000UL;

LiquidCrystal_I2C lcd(LCD_I2C_ADDRESS, LCD_COLUMNS, LCD_ROWS);

unsigned long lastBlinkMillis = 0;
bool backlightPinState = false;

void applyBacklightPinState();
void showStatus();

void setup() {
  pinMode(LCD_BACKLIGHT_PIN, OUTPUT);
  digitalWrite(LCD_BACKLIGHT_PIN, LOW);

  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println(F("LCD backlight pin blink test"));
  Serial.println(F("Pin: D3/GPIO0"));

  Wire.begin(4, 5);
  Wire.setClock(100000);
  lcd.init();
  lcd.backlight();
  lcd.clear();

  showStatus();
}

void loop() {
  unsigned long now = millis();
  if (now - lastBlinkMillis >= BLINK_INTERVAL_MS) {
    lastBlinkMillis = now;
    backlightPinState = !backlightPinState;
    applyBacklightPinState();
    showStatus();
  }
}

void applyBacklightPinState() {
  digitalWrite(LCD_BACKLIGHT_PIN, backlightPinState ? HIGH : LOW);
}

void showStatus() {
  lcd.setCursor(0, 0);
  lcd.print(F("Backlight blink    "));

  lcd.setCursor(0, 1);
  lcd.print(F("Pin D3 GPIO0       "));

  lcd.setCursor(0, 2);
  lcd.print(backlightPinState ? F("State HIGH         ") : F("State LOW          "));

  lcd.setCursor(0, 3);
  lcd.print(F("1 second interval  "));

  Serial.print(F("D3/GPIO0="));
  Serial.println(backlightPinState ? F("HIGH") : F("LOW"));
}
