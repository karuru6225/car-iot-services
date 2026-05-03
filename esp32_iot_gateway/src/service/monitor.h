#pragma once
#include "../domain/measurement.h"
#include "../domain/sensor_factory.h"
#include "../config.h"

struct MeasureResult {
  SensorReading reading;
  SensorVariant ble[QUEUE_SIZE];
  int bleCount = 0;
};

MeasureResult measure();
void publish(const MeasureResult &result);
