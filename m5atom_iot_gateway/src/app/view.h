#pragma once
#include <M5Unified.h> // BLE より先にインクルード
#include "domain/thermometer.h"
#include "domain/co2meter.h"
#include "register_mode.h"

class View
{
public:
  void clear();
  void thermometerData(const ThermometerData &d);
  void co2Data(const Co2MeterData &d);
  void message(const char *msg);
  void registerModeStart();
  void registerNoDevices();
  void registerList(const RegEntry *list, int count, int selected);
  void registerCancel();
  void registerResult(bool wasRegistered, const char *addr);
};

extern View view;
