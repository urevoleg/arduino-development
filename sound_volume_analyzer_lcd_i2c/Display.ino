byte levelBarChars[6][8] = {
  {0, 0, 0, 0, 0, 0, 0, 0},
  {16, 16, 16, 16, 16, 16, 16, 16},
  {24, 24, 24, 24, 24, 24, 24, 24},
  {28, 28, 28, 28, 28, 28, 28, 28},
  {30, 30, 30, 30, 30, 30, 30, 30},
  {31, 31, 31, 31, 31, 31, 31, 31}
};

void initializeDisplay() {
  // Сначала проверяем I2C: отсутствие дисплея не должно останавливать программу.
  lcdConnected = isI2cDeviceConnected(LCD_I2C_ADDRESS);
  if (!lcdConnected) {
    return;
  }

  lcd.init();
  lcd.backlight();
  loadLevelBarChars();
}

void loadLevelBarChars() {
  // Пользовательские символы позволяют рисовать шкалу с шагом 1/5 знакоместа.
  for (byte i = 0; i < 6; i++) {
    lcd.createChar(i, levelBarChars[i]);
  }
}

void showVolume(int volumeLevel, byte volumePercent) {
  if (!lcdConnected) {
    Serial.println(F("LCD error: display not connected"));
    return;
  }

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F("Vol:"));
  printPaddedPercent(volumePercent);
  lcd.print(F("% L:"));
  lcd.print(volumeLevel);

  drawVolumeBar(volumePercent);
}

void printPaddedPercent(byte volumePercent) {
  if (volumePercent < 100) {
    lcd.print(' ');
  }
  if (volumePercent < 10) {
    lcd.print(' ');
  }
  lcd.print(volumePercent);
}

void drawVolumeBar(byte volumePercent) {
  lcd.setCursor(0, 1);

  int filledParts = map(volumePercent, 0, 100, 0, LCD_COLUMNS * 5);

  for (byte column = 0; column < LCD_COLUMNS; column++) {
    int partsInColumn = filledParts - column * 5;
    partsInColumn = constrain(partsInColumn, 0, 5);
    lcd.write((byte)partsInColumn);
  }
}

void showMessage(const char *line1, const char *line2) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(line1);
  lcd.setCursor(0, 1);
  lcd.print(line2);
}
