#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_ADS1X15.h>

// AtomS3 Port A (I2C)
static const uint8_t SDA_PIN     = 38;
static const uint8_t SCL_PIN     = 39;

static const int     SAMPLES     = 20;
static const int     PRINT_EVERY = 10;  // 100ms × 10 = 1秒ごとに表示

// 分圧回路（差動）: +/− → R1(680kΩ) → AIN0/AIN1 → R2(11kΩ) → GND
// DIV_RATIO ≈ 0.01592  →  GAIN_EIGHT(±0.512V) で最大 ≈ ±32V 測定可能
static const float R1        = 680000.0f;
static const float R2        =  11000.0f;
static const float DIV_RATIO = R2 / (R1 + R2);

Adafruit_ADS1115 ads;

int16_t buf[SAMPLES] = {};
int     idx          = 0;
int     count        = 0;
int     printCount   = 0;

void setup()
{
  Serial.begin(115200);
  delay(1000);
  Wire.begin(SDA_PIN, SCL_PIN);
  ads.setGain(GAIN_EIGHT);  // ±0.512V full scale
  if (!ads.begin(0x49)) {
    Serial.println("ADS1115 not found!");
    while (1) delay(1000);
  }
  Serial.println("=== ADS1115 Voltage Monitor (20-sample avg) ===");
}

void loop()
{
  buf[idx] = ads.readADC_Differential_1_0();
  idx = (idx + 1) % SAMPLES;
  if (count < SAMPLES) count++;

  if (++printCount < PRINT_EVERY) { delay(100); return; }
  printCount = 0;

  long sum = 0;
  for (int i = 0; i < count; i++) sum += buf[i];
  float adcV   = ads.computeVolts((int16_t)(sum / count));
  float inputV = adcV / DIV_RATIO;

  Serial.printf("Voltage: %.2f V  (n=%d)\n", inputV, count);
  delay(100);
}
