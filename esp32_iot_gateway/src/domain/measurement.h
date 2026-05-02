#pragma once
#include <stdint.h>

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
