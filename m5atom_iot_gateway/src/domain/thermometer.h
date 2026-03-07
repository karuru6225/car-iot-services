#pragma once
#include "sensor.h"
#include <string>

struct ThermometerData : SensorBase {};

class ThermometerParser
{
public:
  // 温度・湿度・バッテリーのパース（Co2MeterParser でも流用）
  // WoIOSensor Manufacturer Data フォーマット:
  //   [0-1]  Company ID (0x0969)
  //   [2-7]  MAC アドレス
  //   [8]    シーケンス
  //   [9]    不明
  //   [10]   温度小数部 (bit3-0)
  //   [11]   温度整数部 (bit6-0) + 符号 (bit7: 1=正)
  //   [12]   湿度 (%)
  // Service Data フォーマット: [0]=デバイス種別 [1]=ステータス [2]=バッテリー(%)
  static void parseCommon(SensorBase &d,
                          const std::string &mf,
                          const std::string &sd);

  static ThermometerData parse(const char *addr, int8_t rssi,
                                const std::string &mf,
                                const std::string &sd);
};
