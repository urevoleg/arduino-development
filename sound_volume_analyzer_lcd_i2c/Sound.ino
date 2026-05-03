int soundLevelSamples[AVERAGE_SAMPLE_COUNT];
byte soundLevelSampleIndex = 0;
byte soundLevelSampleCount = 0;
long soundLevelSampleSum = 0;

int calibrateSoundBaseline() {
  unsigned long startedAt = millis();
  unsigned long sampleCount = 0;
  unsigned long sampleSum = 0;

  // Во время калибровки датчик должен находиться в тишине: это станет базовым уровнем.
  while (millis() - startedAt < CALIBRATION_DURATION) {
    sampleSum += analogRead(SOUND_SENSOR_PIN);
    sampleCount++;
  }

  if (sampleCount == 0) {
    return 0;
  }

  return sampleSum / sampleCount;
}

int readSoundLevel() {
  // Абсолютный уровень берем напрямую с аналогового выхода датчика: 0..1023.
  return analogRead(SOUND_SENSOR_PIN);
}

int addSoundLevelSample(int rawLevel) {
  rawLevel = constrain(rawLevel, ADC_MIN_VALUE, ADC_MAX_VALUE);

  if (soundLevelSampleCount < AVERAGE_SAMPLE_COUNT) {
    soundLevelSamples[soundLevelSampleIndex] = rawLevel;
    soundLevelSampleSum += rawLevel;
    soundLevelSampleCount++;
  } else {
    soundLevelSampleSum -= soundLevelSamples[soundLevelSampleIndex];
    soundLevelSamples[soundLevelSampleIndex] = rawLevel;
    soundLevelSampleSum += rawLevel;
  }

  soundLevelSampleIndex++;
  if (soundLevelSampleIndex >= AVERAGE_SAMPLE_COUNT) {
    soundLevelSampleIndex = 0;
  }

  return soundLevelSampleSum / soundLevelSampleCount;
}

int reduceNoise(int averageLevel) {
  int volumeLevel = averageLevel - soundBaseline - NOISE_REDUCTION;

  if (volumeLevel < 0) {
    return 0;
  }

  return volumeLevel;
}

byte convertVolumeToPercent(int volumeLevel) {
  volumeLevel = constrain(volumeLevel, 0, MAX_VOLUME_LEVEL);
  return map(volumeLevel, 0, MAX_VOLUME_LEVEL, 0, 100);
}
