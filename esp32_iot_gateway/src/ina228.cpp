#include "ina228.h"
#include <Wire.h>

static const uint8_t ADDR = 0x40;
static const float CURRENT_LSB = 208e-6f; // A/bit

static bool initialized = false;

static void wr16(uint8_t reg, uint16_t v)
{
  Wire.beginTransmission(ADDR);
  Wire.write(reg);
  Wire.write(v >> 8);
  Wire.write(v & 0xFF);
  Wire.endTransmission();
}

static uint16_t rd16(uint8_t reg)
{
  Wire.beginTransmission(ADDR);
  Wire.write(reg);
  Wire.endTransmission(false);
  Wire.requestFrom(ADDR, (uint8_t)2);
  return ((uint16_t)Wire.read() << 8) | Wire.read();
}

static int32_t rd24s(uint8_t reg)
{
  Wire.beginTransmission(ADDR);
  Wire.write(reg);
  Wire.endTransmission(false);
  Wire.requestFrom(ADDR, (uint8_t)3);
  uint32_t r = ((uint32_t)Wire.read() << 16) | ((uint32_t)Wire.read() << 8) | Wire.read();
  return (int32_t)(r << 8) >> 8; // 24bit → 32bit 符号拡張
}

static uint32_t rd24u(uint8_t reg)
{
  Wire.beginTransmission(ADDR);
  Wire.write(reg);
  Wire.endTransmission(false);
  Wire.requestFrom(ADDR, (uint8_t)3);
  return ((uint32_t)Wire.read() << 16) | ((uint32_t)Wire.read() << 8) | Wire.read();
}

bool ina228Init()
{
  uint16_t mfr = rd16(0xFE);
  uint16_t dev = rd16(0xFF);
  Serial.printf("[INA228] MFR=0x%04X DEV=0x%04X\n", mfr, dev);

  if (mfr != 0x5449)
  { // "TI"
    Serial.println("[INA228] not found (expected MFR=0x5449)");
    Serial.println("[INA228] I2C scan:");
    for (uint8_t a = 1; a < 127; a++)
    {
      Wire.beginTransmission(a);
      if (Wire.endTransmission() == 0)
        Serial.printf("  found: 0x%02X\n", a);
    }
    return false;
  }

  // ADCRANGE=1 (CONFIG reg bit4)、その他はデフォルトのまま
  wr16(0x00, 1 << 4);
  // SHUNT_CAL = 255 (ADCRANGE=1、シャント 0.375mΩ、CURRENT_LSB = 208μA)
  wr16(0x02, 255);

  uint16_t cfg = rd16(0x00);
  uint16_t cal = rd16(0x02);
  Serial.printf("[INA228] OK  CONFIG=0x%04X  SHUNT_CAL=0x%04X\n", cfg, cal);

  initialized = true;
  return true;
}

float ina228ReadVBus()
{
  // bits[23:4] = 20bit unsigned、LSB = 195.3125μV
  return (rd24u(0x05) >> 4) * 195.3125e-6f;
}

float ina228ReadCurrent()
{
  // bits[23:4] = 20bit signed、LSB = CURRENT_LSB
  return (rd24s(0x07) >> 4) * CURRENT_LSB;
}

float ina228ReadPower()
{
  // 24bit unsigned、P = raw × 3.2 × CURRENT_LSB
  return rd24u(0x08) * 3.2f * CURRENT_LSB;
}

float ina228ReadTemp()
{
  return ((int16_t)rd16(0x06)) * 7.8125e-3f;
}

void ina228PrintStatus()
{
  if (!initialized)
  {
    Serial.println("[INA228] not initialized");
    return;
  }
  Serial.printf("[INA228] Vbus=%.3fV  Cur=%.4fA  Pwr=%.3fW  Temp=%.1fC\n",
                ina228ReadVBus(), ina228ReadCurrent(), ina228ReadPower(), ina228ReadTemp());
}
