#include "co2meter.h"
#include "thermometer.h"

Co2MeterData Co2MeterParser::parse(const char *addr, int8_t rssi,
                                    const std::string &mf,
                                    const std::string &sd)
{
  Co2MeterData d = {};
  strncpy(d.address, addr, sizeof(d.address) - 1);
  d.rssi = rssi;
  ThermometerParser::parseCommon(d, mf, sd);
  if (mf.length() >= 17)
    d.co2 = ((uint16_t)(uint8_t)mf[15] << 8) | (uint8_t)mf[16];
  return d;
}
