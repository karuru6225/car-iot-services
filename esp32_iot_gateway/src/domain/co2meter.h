#pragma once
#include "sensor.h"
#include <string>

struct Co2MeterData : SensorBase
{
  uint16_t co2 = 0;
};

class Co2MeterParser
{
public:
  // CO2センサー Manufacturer Data フォーマット（WoIOSensorと共通部分あり）:
  //   [0-1]  Company ID (0x0969)
  //   [2-7]  MAC アドレス
  //   [10]   温度小数部 (bit3-0)   ← WoIOSensor と同じ
  //   [11]   温度整数部 + 符号      ← WoIOSensor と同じ
  //   [12]   湿度 (%)              ← WoIOSensor と同じ
  //   [15-16] CO2 (ppm, big-endian uint16)
  // Service Data フォーマット: [0]=0x35(CO2センサー種別) [1]=ステータス [2]=バッテリー(%)
  static Co2MeterData parse(const char *addr, int8_t rssi,
                             const std::string &mf,
                             const std::string &sd);
};
