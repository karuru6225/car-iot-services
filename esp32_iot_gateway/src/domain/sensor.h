#pragma once
#include <Arduino.h>

// BLE センサーデータの共通フィールド
// ThermometerData / Co2MeterData が継承する
// FreeRTOS キューで memcopy されるため virtual 禁止
struct SensorBase
{
  char    address[18] = {};
  int8_t  rssi        = 0;
  float   temp        = 0.0f;
  uint8_t humidity    = 0;
  uint8_t battery     = 0;
  bool    parsed      = false;
  char    mfHex[40]   = {}; // Manufacturer Data 生バイト列（小文字 hex、最大 19 バイト分）
};
