#pragma once
#include <NimBLEDevice.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "../config.h"
#include "../domain/sensor_factory.h"

class BleScanner
{
public:
  QueueHandle_t queue = nullptr;
  bool registrationMode = false; // true のとき bleTargets フィルタをスキップ

  void setup();
  void start(int seconds);
  void clearResults();
  void deinit();

private:
  NimBLEScan *_scan = nullptr;
};

extern BleScanner bleScanner;
