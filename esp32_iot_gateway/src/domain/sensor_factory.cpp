#include "sensor_factory.h"

static constexpr uint8_t CO2_METER_DEVICE_TYPE = 0x35;

SensorVariant SensorParserFactory::parse(const char *addr, int8_t rssi,
                                          const std::string &mf,
                                          const std::string &sd)
{
  if (sd.length() >= 1 && (uint8_t)sd[0] == CO2_METER_DEVICE_TYPE)
    return Co2MeterParser::parse(addr, rssi, mf, sd);
  return ThermometerParser::parse(addr, rssi, mf, sd);
}
