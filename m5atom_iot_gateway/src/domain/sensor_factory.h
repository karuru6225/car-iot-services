#pragma once
#include <variant>
#include "thermometer.h"
#include "co2meter.h"
#include <string>

using SensorVariant = std::variant<ThermometerData, Co2MeterData>;

class SensorParserFactory
{
public:
  // serviceData[0] == 0x35 → Co2MeterParser
  // それ以外              → ThermometerParser
  static SensorVariant parse(const char *addr, int8_t rssi,
                              const std::string &mf,
                              const std::string &sd);
};
