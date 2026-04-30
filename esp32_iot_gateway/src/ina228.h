#pragma once
#include <Arduino.h>

bool ina228Init();
float ina228ReadVBus();    // V
float ina228ReadCurrent(); // A (CURRENT_LSB = 208μA/bit)
float ina228ReadPower();   // W
float ina228ReadTemp();    // °C
void ina228PrintStatus();  // Serial にすべての値を出力
