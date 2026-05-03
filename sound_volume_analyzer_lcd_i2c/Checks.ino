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
    loadLevelBarChars();
    showMessage("LCD connected", "");
  } else if (!lcdConnected && wasConnected) {
    Serial.println(F("LCD error: display disconnected"));
  }
}

void blinkActivity() {
  // Только запускаем вспышку: выключение делает updateActivityLed() без блокировки.
  digitalWrite(STATUS_LED_PIN, HIGH);
  activityLedStartedAt = millis();
  activityLedActive = true;
}

void updateActivityLed(unsigned long now) {
  if (!activityLedActive) {
    return;
  }

  if (now - activityLedStartedAt >= ACTIVITY_LED_DURATION) {
    digitalWrite(STATUS_LED_PIN, LOW);
    activityLedActive = false;
  }
}
