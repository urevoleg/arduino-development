void initializePressureSensor() {
  bmp180Connected = isI2cDeviceConnected(BMP180_I2C_ADDRESS) && bmp180.begin();

  if (!bmp180Connected) {
    Serial.println(F("Sensor error: BMP180 initialization failed"));
  }
}

bool readPressureHpa(float *pressureHpa) {
  // BMP180 висит на I2C, поэтому сначала проверяем, отвечает ли устройство.
  if (!isI2cDeviceConnected(BMP180_I2C_ADDRESS)) {
    bmp180Connected = false;
    Serial.println(F("Sensor error: BMP180 not connected"));
    return false;
  }

  if (!bmp180Connected) {
    bmp180Connected = bmp180.begin();
    if (!bmp180Connected) {
      Serial.println(F("Sensor error: BMP180 reinitialization failed"));
      return false;
    }
  }

  long pressurePa = bmp180.readPressure();
  if (pressurePa <= 0) {
    Serial.println(F("Sensor error: BMP180 pressure read failed"));
    return false;
  }

  *pressureHpa = pressurePa / 100.0;
  return true;
}

float pressureHpaToMmHg(float pressureHpa) {
  return pressureHpa * 0.750062;
}
