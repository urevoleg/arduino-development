int16_t temperatureSamples[AVERAGE_SAMPLE_COUNT];
uint16_t temperatureSampleIndex = 0;
uint16_t temperatureSampleCount = 0;
long temperatureSampleSum = 0;

int16_t temperatureToRaw(float temperatureC) {
  // Храним сотые доли градуса, чтобы 5-минутный буфер занимал мало RAM.
  return (int16_t)(temperatureC * 100.0);
}

float rawTemperatureToC(int16_t rawTemperature) {
  return rawTemperature / 100.0;
}

void addTemperatureSample(float temperatureC) {
  int16_t rawTemperature = temperatureToRaw(temperatureC);

  if (temperatureSampleCount < AVERAGE_SAMPLE_COUNT) {
    temperatureSamples[temperatureSampleIndex] = rawTemperature;
    temperatureSampleSum += rawTemperature;
    temperatureSampleCount++;
  } else {
    temperatureSampleSum -= temperatureSamples[temperatureSampleIndex];
    temperatureSamples[temperatureSampleIndex] = rawTemperature;
    temperatureSampleSum += rawTemperature;
  }

  temperatureSampleIndex++;
  if (temperatureSampleIndex >= AVERAGE_SAMPLE_COUNT) {
    temperatureSampleIndex = 0;
  }
}

bool hasAverageTemperature() {
  return temperatureSampleCount > 0;
}

bool isAverageWindowFilled() {
  return temperatureSampleCount >= AVERAGE_SAMPLE_COUNT;
}

uint16_t getTemperatureSampleCount() {
  return temperatureSampleCount;
}

int16_t getTemperatureSampleByAge(uint16_t ageFromOldest) {
  if (ageFromOldest >= temperatureSampleCount) {
    return TEMPERATURE_INVALID;
  }

  uint16_t oldestIndex = temperatureSampleCount < AVERAGE_SAMPLE_COUNT ? 0 : temperatureSampleIndex;
  uint16_t index = (oldestIndex + ageFromOldest) % AVERAGE_SAMPLE_COUNT;
  return temperatureSamples[index];
}

int16_t getAverageTemperatureRaw() {
  if (!hasAverageTemperature()) {
    return TEMPERATURE_INVALID;
  }

  return (int16_t)(temperatureSampleSum / temperatureSampleCount);
}
