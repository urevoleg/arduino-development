int16_t pressureWindowSamples[PRESSURE_WINDOW_SAMPLE_COUNT];
int16_t pressureHistoryWindows[PRESSURE_HISTORY_WINDOW_COUNT];
uint16_t pressureWindowSampleIndex = 0;
uint16_t pressureWindowSampleCount = 0;
uint16_t pressureWindowSamplesSinceStore = 0;
long pressureWindowSampleSum = 0;
byte pressureHistoryNextIndex = 0;
byte pressureHistoryWindowCount = 0;

int16_t pressureToRaw(float pressureMmHg) {
  // Храним десятые доли мм рт. ст., чтобы среднее занимало мало RAM.
  return (int16_t)(pressureMmHg * 10.0);
}

float rawPressureToMmHg(int16_t rawPressure) {
  return rawPressure / 10.0;
}

void storePressureWindow(int16_t rawPressure);

void addPressureSample(float pressureMmHg) {
  int16_t rawPressure = pressureToRaw(pressureMmHg);

  if (pressureWindowSampleCount < PRESSURE_WINDOW_SAMPLE_COUNT) {
    pressureWindowSamples[pressureWindowSampleIndex] = rawPressure;
    pressureWindowSampleSum += rawPressure;
    pressureWindowSampleCount++;
  } else {
    pressureWindowSampleSum -= pressureWindowSamples[pressureWindowSampleIndex];
    pressureWindowSamples[pressureWindowSampleIndex] = rawPressure;
    pressureWindowSampleSum += rawPressure;
  }

  pressureWindowSampleIndex++;
  if (pressureWindowSampleIndex >= PRESSURE_WINDOW_SAMPLE_COUNT) {
    pressureWindowSampleIndex = 0;
  }

  pressureWindowSamplesSinceStore++;
  if (pressureWindowSamplesSinceStore >= PRESSURE_WINDOW_SAMPLE_COUNT) {
    storePressureWindow(getAveragePressureRaw());
    pressureWindowSamplesSinceStore = 0;
  }
}

bool hasAveragePressure() {
  return pressureWindowSampleCount > 0;
}

int16_t getAveragePressureRaw() {
  if (!hasAveragePressure()) {
    return PRESSURE_INVALID;
  }

  return (int16_t)(pressureWindowSampleSum / pressureWindowSampleCount);
}

void storePressureWindow(int16_t rawPressure) {
  if (rawPressure == PRESSURE_INVALID) {
    return;
  }

  pressureHistoryWindows[pressureHistoryNextIndex] = rawPressure;
  pressureHistoryNextIndex++;
  if (pressureHistoryNextIndex >= PRESSURE_HISTORY_WINDOW_COUNT) {
    pressureHistoryNextIndex = 0;
  }

  if (pressureHistoryWindowCount < PRESSURE_HISTORY_WINDOW_COUNT) {
    pressureHistoryWindowCount++;
  }
}

byte getStoredPressureWindowCount() {
  return pressureHistoryWindowCount;
}

int16_t getStoredPressureWindowByAge(byte ageFromOldest) {
  if (ageFromOldest >= pressureHistoryWindowCount) {
    return PRESSURE_INVALID;
  }

  byte oldestIndex = pressureHistoryWindowCount < PRESSURE_HISTORY_WINDOW_COUNT ? 0 : pressureHistoryNextIndex;
  byte index = (oldestIndex + ageFromOldest) % PRESSURE_HISTORY_WINDOW_COUNT;
  return pressureHistoryWindows[index];
}

float getPressureChangeSpeedMmHgPerHour() {
  if (pressureHistoryWindowCount < 2) {
    return 0.0;
  }

  int16_t oldestPressure = getStoredPressureWindowByAge(0);
  int16_t newestPressure = getStoredPressureWindowByAge(pressureHistoryWindowCount - 1);
  if (oldestPressure == PRESSURE_INVALID || newestPressure == PRESSURE_INVALID) {
    return 0.0;
  }

  byte intervals = pressureHistoryWindowCount - 1;
  float hours = (intervals * PRESSURE_WINDOW) / 3600000.0;
  if (hours <= 0.0) {
    return 0.0;
  }

  return rawPressureToMmHg(newestPressure - oldestPressure) / hours;
}

char getPressureTrendSymbol() {
  if (pressureHistoryWindowCount < 2) {
    return '?';
  }

  float speed = getPressureChangeSpeedMmHgPerHour();
  if (speed > PRESSURE_STABLE_SPEED_THRESHOLD) {
    return '^';
  }
  if (speed < -PRESSURE_STABLE_SPEED_THRESHOLD) {
    return 'v';
  }

  return '-';
}
