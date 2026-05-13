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
  float ah;      // Ah
};

struct SensorReading
{
  VoltageReading main;
  VoltageReading sub;
  PowerReading pwr;
  time_t ts;
};
