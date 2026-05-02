#include "ads.h"
#include <Wire.h>
#include <Adafruit_ADS1X15.h>

// 680kΩ + 22kΩ 分圧：Vin = Vout × (680+22)/22
static constexpr float DIVIDER_RATIO = (680000.0f + 22000.0f) / 22000.0f;

static Adafruit_ADS1115 ads;

bool adsInit()
{
  // Wire は oledInit() で begin 済みなので再呼び出し不要
  if (!ads.begin(0x48, &Wire))
    return false;
  // 最大 Vout ≈ 0.470V（Vin=15V 時）→ GAIN_FOUR（±1.024V）で余裕あり
  // 入力換算分解能 ≈ 1mV/bit
  ads.setGain(GAIN_FOUR);
  return true;
}

// 戻り値：分圧前の実際の電圧（V）
float adsReadDiff01()
{
  int16_t raw = ads.readADC_Differential_0_1();
  return ads.computeVolts(raw) * DIVIDER_RATIO;
}

float adsReadDiff23()
{
  int16_t raw = ads.readADC_Differential_2_3();
  return ads.computeVolts(raw) * DIVIDER_RATIO;
}