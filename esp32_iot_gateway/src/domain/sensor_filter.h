#pragma once

#ifdef BLE_MEDIAN_FILTER

#include "sensor_factory.h"
#include "../config.h"

struct SensorHistory {
  char     address[18];
  float    temps[3];
  uint8_t  hums[3];
  uint16_t co2s[3];
  uint8_t  count;  // 蓄積済み件数（0〜3でキャップ）
  uint8_t  idx;    // 次に書き込む位置（循環）
};

class SensorFilter {
public:
  void begin();
  void apply(SensorVariant &v);

private:
  SensorHistory *findOrCreate(const char *addr);
};

extern SensorFilter sensorFilter;

#endif // BLE_MEDIAN_FILTER
