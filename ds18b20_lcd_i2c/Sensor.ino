void initializeSensor() {
  sensors.begin();

  // Так можно обнаружить отсутствующий DS18B20 еще до чтения температуры.
  sensorConnected = sensors.getDeviceCount() > 0;
}

bool readTemperatureC(float *temperatureC) {
  // Датчик может быть отключен уже после запуска программы.
  sensorConnected = sensors.getDeviceCount() > 0;
  if (!sensorConnected) {
    showError("Sensor error", "Not connected");
    Serial.println(F("Sensor error: DS18B20 not connected"));
    return false;
  }

  sensors.requestTemperatures();
  *temperatureC = sensors.getTempCByIndex(0);

  // Библиотека возвращает это значение, если датчик не смог отдать данные.
  if (*temperatureC == DEVICE_DISCONNECTED_C) {
    showError("Sensor error", "Read failed");
    Serial.println(F("Sensor error: DS18B20 read failed"));
    return false;
  }

  return true;
}
