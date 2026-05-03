byte temperatureGraphChars[8][8] = {
  {0, 0, 0, 0, 0, 0, 0, 31},
  {0, 0, 0, 0, 0, 0, 31, 31},
  {0, 0, 0, 0, 0, 31, 31, 31},
  {0, 0, 0, 0, 31, 31, 31, 31},
  {0, 0, 0, 31, 31, 31, 31, 31},
  {0, 0, 31, 31, 31, 31, 31, 31},
  {0, 31, 31, 31, 31, 31, 31, 31},
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
  loadTemperatureGraphChars();
}

void loadTemperatureGraphChars() {
  // 8 пользовательских символов дают столбики графика высотой от 1 до 8 пикселей.
  for (byte i = 0; i < 8; i++) {
    lcd.createChar(i, temperatureGraphChars[i]);
  }
}

void showMeasurements(float temperatureC, float averageTemperatureC, float pressureMmHg) {
  // Если LCD недоступен, измерения остаются видны в Serial Monitor.
  if (!lcdConnected) {
    Serial.println(F("LCD error: display not connected"));
    return;
  }

  lcd.clear();
  lcd.setCursor(0, 0);
  if (temperatureC <= TEMPERATURE_DISPLAY_INVALID) {
    lcd.print(F("T:err"));
  } else {
    drawTemperatureGraphLine(temperatureC);
  }

  lcd.setCursor(0, 1);
  if (pressureMmHg < 0.0) {
    lcd.print(F("P:err"));
    return;
  }

  lcd.print(F("P:"));
  lcd.print(pressureMmHg, 0);
  lcd.print(getPressureTrendSymbol());
  lcd.print(F(" "));
  printPressureSpeed(getPressureChangeSpeedMmHgPerHour());
  lcd.print(F("/h"));
}

void drawTemperatureGraphLine(float temperatureC) {
  uint16_t count = getTemperatureSampleCount();
  byte graphColumns = min((uint16_t)TEMPERATURE_GRAPH_COLUMNS, count);
  byte emptyColumns = TEMPERATURE_GRAPH_COLUMNS - graphColumns;

  int16_t minTemperature = TEMPERATURE_INVALID;
  int16_t maxTemperature = TEMPERATURE_INVALID;

  for (byte i = 0; i < graphColumns; i++) {
    uint16_t sampleAge = count - graphColumns + i;
    int16_t rawTemperature = getTemperatureSampleByAge(sampleAge);

    if (rawTemperature == TEMPERATURE_INVALID) {
      continue;
    }
    if (minTemperature == TEMPERATURE_INVALID || rawTemperature < minTemperature) {
      minTemperature = rawTemperature;
    }
    if (maxTemperature == TEMPERATURE_INVALID || rawTemperature > maxTemperature) {
      maxTemperature = rawTemperature;
    }
  }

  int16_t range = maxTemperature - minTemperature;
  for (byte i = 0; i < graphColumns; i++) {
    uint16_t sampleAge = count - graphColumns + i;
    int16_t rawTemperature = getTemperatureSampleByAge(sampleAge);
    byte level = 3;

    if (range > 0) {
      level = map(rawTemperature, minTemperature, maxTemperature, 0, 7);
    }

    lcd.write((byte)level);
  }

  for (byte i = 0; i < emptyColumns; i++) {
    lcd.print(' ');
  }

  printTemperatureInt(temperatureC);
}

void printPressureSpeed(float pressureSpeed) {
  if (pressureSpeed > 0.0) {
    lcd.print('+');
  }
  lcd.print(pressureSpeed, 1);
}

void printTemperatureInt(float temperatureC) {
  int temperatureInt = (int)(temperatureC >= 0 ? temperatureC + 0.5 : temperatureC - 0.5);
  temperatureInt = constrain(temperatureInt, -99, 99);

  if (temperatureInt >= 0) {
    if (temperatureInt < 10) {
      lcd.print(' ');
      lcd.print(' ');
    } else {
      lcd.print(' ');
    }
    lcd.print(temperatureInt);
  } else {
    if (temperatureInt > -10) {
      lcd.print(' ');
    }
    lcd.print(temperatureInt);
  }
}

void showError(const char *line1, const char *line2) {
  // Ошибки выводятся на LCD, если он доступен, иначе в Serial.
  if (lcdConnected) {
    showMessage(line1, line2);
  } else {
    Serial.println(F("LCD error: display not connected"));
  }
}

void showMessage(const char *line1, const char *line2) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(line1);
  lcd.setCursor(0, 1);
  lcd.print(line2);
}
