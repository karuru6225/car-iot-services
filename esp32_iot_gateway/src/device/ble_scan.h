#pragma once
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "../config.h"
#include "../domain/sensor_factory.h"

class BleScanner
{
public:
  QueueHandle_t queue = nullptr;

  void setup();
  void start(int seconds);
  void clearResults();
  void deinit();

private:
  BLEScan *_scan = nullptr;
};

extern BleScanner bleScanner;
