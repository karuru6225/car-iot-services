#pragma once
#include <M5Unified.h> // BLE より先にインクルード
#include "infra__ble_scan.h"
#include "register_mode.h"

class View
{
public:
  void clear();
  void sensorData(const SwitchBotData &d);
  void message(const char *msg);
  void registerModeStart();
  void registerNoDevices();
  void registerList(const RegEntry *list, int count, int selected);
  void registerCancel();
  void registerResult(bool wasRegistered, const char *addr);
};

extern View view;
