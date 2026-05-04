#include "ina228.h"
#include <Wire.h>

Ina228 ina228;

void Ina228::wr16(uint8_t reg, uint16_t v)
{
  Wire.beginTransmission(ADDR);
  Wire.write(reg);
  Wire.write(v >> 8);
  Wire.write(v & 0xFF);
  Wire.endTransmission();
}

uint16_t Ina228::rd16(uint8_t reg)
{
  Wire.beginTransmission(ADDR);
  Wire.write(reg);
  Wire.endTransmission(false);
  Wire.requestFrom(ADDR, (uint8_t)2);
  return ((uint16_t)Wire.read() << 8) | Wire.read();
}

int32_t Ina228::rd24s(uint8_t reg)
{
  Wire.beginTransmission(ADDR);
  Wire.write(reg);
  Wire.endTransmission(false);
  Wire.requestFrom(ADDR, (uint8_t)3);
  uint32_t r = ((uint32_t)Wire.read() << 16) | ((uint32_t)Wire.read() << 8) | Wire.read();
  return (int32_t)(r << 8) >> 8; // 24bit → 32bit 符号拡張
}

uint32_t Ina228::rd24u(uint8_t reg)
{
  Wire.beginTransmission(ADDR);
  Wire.write(reg);
  Wire.endTransmission(false);
  Wire.requestFrom(ADDR, (uint8_t)3);
  return ((uint32_t)Wire.read() << 16) | ((uint32_t)Wire.read() << 8) | Wire.read();
}

uint64_t Ina228::rd40u(uint8_t reg)
{
  Wire.beginTransmission(ADDR);
  Wire.write(reg);
  Wire.endTransmission(false);
  Wire.requestFrom(ADDR, (uint8_t)5);
  return ((uint64_t)Wire.read() << 32) | ((uint64_t)Wire.read() << 24) |
         ((uint64_t)Wire.read() << 16) | ((uint64_t)Wire.read() << 8) | Wire.read();
}

bool Ina228::init()
{
  uint16_t mfr = rd16(0xFE);
  uint16_t dev = rd16(0xFF);
  Serial.printf("[INA228] MFR=0x%04X DEV=0x%04X\n", mfr, dev);

  if (mfr != 0x5449)
  {
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

  // R_shunt = 75mV / 200A = 0.375mΩ
  // CURRENT_LSB = 208μA
  // SHUNT_CAL = 819.2×10^6 × CURRENT_LSB × R_shunt × 4（ADCRANGE=1 のため×4）
  //           = 819.2e6 × 208e-6 × 0.375e-3 × 4 ≒ 255 × 16 = 4096
  // ADCRANGE=1 → ±40.96mV、フルスケール電流 ±109A (I = V/R_shunt = 40.96mV / 0.375mΩ = 109A)
  wr16(0x00, CONFIG);
  wr16(0x02, SHUNT_CAL);

  uint16_t cfg = rd16(0x00);
  uint16_t cal = rd16(0x02);
  Serial.printf("[INA228] OK  CONFIG=0x%04X  SHUNT_CAL=0x%04X\n", cfg, cal);

  _initialized = true;
  return true;
}

float Ina228::readShuntVoltage()
{
  // ADCRANGE=1 -> 78.125nV/LSB
  return (rd24s(0x04) >> 4) * 78.125e-9f;
}

float Ina228::readVBus()
{
  // bits[23:4] = 20bit unsigned、LSB = 195.3125μV
  return (rd24u(0x05) >> 4) * 195.3125e-6f;
}

float Ina228::readCurrent()
{
  // bits[23:4] = 20bit signed、LSB = CURRENT_LSB
  // 電流値もともに符号反転（レジスタの増減方向が直感と逆）なので、-1 を掛ける
  return -(rd24s(0x07) >> 4) * CURRENT_LSB;
}

float Ina228::readPower()
{
  // 24bit unsigned、P = raw × 3.2 × CURRENT_LSB
  return rd24u(0x08) * 3.2f * CURRENT_LSB;
}

float Ina228::readTemp()
{
  return ((int16_t)rd16(0x06)) * 7.8125e-3f;
}

float Ina228::readCharge()
{
  // CHARGE レジスタは 40bit unsigned でクーロン値を保持
  uint64_t rawCharge = rd40u(0x0A);
  // 2^39 より大きい値は負数として扱う（符号なし→符号付き変換）
  int64_t signedCharge = (rawCharge >= (1ull << 39))
                             ? (int64_t)(rawCharge - (1ull << 40))
                             : (int64_t)rawCharge;
  return -(signedCharge * CURRENT_LSB / 3600.0f); // 符号反転: レジスタの増減方向が直感と逆
}

void Ina228::resetCharge()
{
  wr16(0x00, CONFIG | (1 << 14)); // RESET_CHARGE ビット
}

void Ina228::printStatus()
{
  if (!_initialized)
  {
    Serial.println("[INA228] not initialized");
    return;
  }
  Serial.printf("[INA228] Vbus=%.3fV  Cur=%.4fA  Pwr=%.3fW  Temp=%.1fC  Charge=%.4fAh  ShuntV=%.3f mV\n",
                readVBus(), readCurrent(), readPower(), readTemp(), readCharge(), readShuntVoltage() * 1000.0f);
}
