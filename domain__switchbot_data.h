#pragma once
#include <Arduino.h>

struct SwitchBotData
{
  char    address[18];
  int8_t  rssi;
  float   temp     = 0.0f;
  uint8_t humidity = 0;
  uint8_t battery  = 0;
  bool    parsed   = false;

  // BLE 生データ → Value Object
  // serviceData は haveServiceData() が false なら空文字列を渡す
  static SwitchBotData parse(const char *addr, int8_t rssi,
                              const String &mfData,
                              const String &serviceData);
};
