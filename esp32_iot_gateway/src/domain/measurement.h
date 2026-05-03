#pragma once
#include <stdint.h>
#include <time.h>

struct VoltageReading
{
  float voltage; // V
};

struct PowerReading
{
  float current; // A
  float power;   // W
  float temp;    // °C
};

struct SensorReading
{
  VoltageReading v1;
  VoltageReading v2;
  PowerReading pwr;
  time_t ts;
};
