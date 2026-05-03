#pragma once
#include "../domain/measurement.h"

SensorReading measure();
void publish(const SensorReading &reading);
