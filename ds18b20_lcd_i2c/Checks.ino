bool isI2cDeviceConnected(byte address) {
  // Wire.endTransmission() возвращает 0, только если устройство ответило на этом адресе.
  Wire.beginTransmission(address);
  return Wire.endTransmission() == 0;
}

void checkLcdConnection() {
  // Периодически проверяем LCD, чтобы вывод восстановился после повторного подключения.
  bool wasConnected = lcdConnected;
  lcdConnected = isI2cDeviceConnected(LCD_I2C_ADDRESS);

  if (lcdConnected && !wasConnected) {
    lcd.init();
    lcd.backlight();
    loadTemperatureGraphChars();
    showMessage("LCD connected", "");
  } else if (!lcdConnected && wasConnected) {
    Serial.println(F("LCD error: display disconnected"));
  }
}

void blinkActivity() {
  if (errorBlinkActive) {
    return;
  }

  digitalWrite(STATUS_LED_PIN, HIGH);
  activityLedStartedAt = millis();
  activityLedActive = true;
}

void blinkError() {
  // Только запускаем серию миганий; updateStatusLed() выполняет ее без блокировки.
  activityLedActive = false;
  errorBlinkActive = true;
  errorBlinkLedState = true;
  errorBlinkTogglesRemaining = ERROR_BLINK_TOGGLES;
  errorBlinkLastToggle = millis();
  digitalWrite(STATUS_LED_PIN, HIGH);
}

void updateStatusLed(unsigned long now) {
  if (errorBlinkActive) {
    if (now - errorBlinkLastToggle < ERROR_BLINK_INTERVAL) {
      return;
    }

    errorBlinkLastToggle = now;
    errorBlinkLedState = !errorBlinkLedState;
    digitalWrite(STATUS_LED_PIN, errorBlinkLedState ? HIGH : LOW);

    if (errorBlinkTogglesRemaining > 0) {
      errorBlinkTogglesRemaining--;
    }
    if (errorBlinkTogglesRemaining == 0) {
      errorBlinkActive = false;
      errorBlinkLedState = false;
      digitalWrite(STATUS_LED_PIN, LOW);
    }
    return;
  }

  if (!activityLedActive) {
    return;
  }

  if (now - activityLedStartedAt >= ACTIVITY_LED_DURATION) {
    activityLedActive = false;
    digitalWrite(STATUS_LED_PIN, LOW);
  }
}
