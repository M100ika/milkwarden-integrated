/*
 * AD7797 Reader
 *
 * Implements the load-cell pipeline referenced in flowchart #3:
 * zeroing → filtering → calibration → transmission.
 */

#include <SPI.h>

// SPI pins depend on board; use defaults for Arduino Pro.
const int CS_PIN = 10;
const int DRDY_PIN = 9;
const int LED_PIN = 13;

float zeroOffset = 0.0f;
float scaleFactor = 1.0f;

float movingAverage(float newValue) {
  static float buffer[8];
  static uint8_t idx = 0;
  buffer[idx] = newValue;
  idx = (idx + 1) % 8;

  float sum = 0.0f;
  for (float value : buffer) {
    sum += value;
  }
  return sum / 8.0f;
}

uint32_t readAdcRaw() {
  digitalWrite(CS_PIN, LOW);
  uint32_t value = 0;
  value |= SPI.transfer(0x00) << 16;
  value |= SPI.transfer(0x00) << 8;
  value |= SPI.transfer(0x00);
  digitalWrite(CS_PIN, HIGH);
  return value;
}

float convertToKg(uint32_t raw) {
  // AD7797 is 24-bit; normalize to 0..1 range.
  float normalized = raw / 16777215.0f;
  float weight = (normalized - zeroOffset) * scaleFactor;
  return weight;
}

void zeroScale(uint16_t samples = 128) {
  float acc = 0.0f;
  for (uint16_t i = 0; i < samples; ++i) {
    acc += readAdcRaw() / 16777215.0f;
    delay(5);
  }
  zeroOffset = acc / samples;
}

void calibrate(float knownWeightKg) {
  float reading = convertToKg(readAdcRaw());
  scaleFactor = knownWeightKg / (reading + 1e-6f);
}

void transmit(float weightKg) {
  Serial.print("{\"weight_kg\":");
  Serial.print(weightKg, 3);
  Serial.println("}");
}

void waitDrdy() {
  while (digitalRead(DRDY_PIN) == HIGH) {
    delayMicroseconds(50);
  }
}

void setup() {
  pinMode(CS_PIN, OUTPUT);
  pinMode(DRDY_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  Serial.begin(115200);
  SPI.begin();
  delay(50);
  zeroScale();
  calibrate(20.0f);  // Example reference mass
}

void loop() {
  waitDrdy();
  uint32_t raw = readAdcRaw();
  float kg = convertToKg(raw);
  float filtered = movingAverage(kg);
  transmit(filtered);
  digitalWrite(LED_PIN, HIGH);
  delay(5);
  digitalWrite(LED_PIN, LOW);
  delay(195);
}
