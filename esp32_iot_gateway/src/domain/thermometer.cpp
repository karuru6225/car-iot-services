#include "thermometer.h"

void ThermometerParser::parseCommon(SensorBase &d,
                                    const std::string &mf,
                                    const std::string &sd)
{
  size_t hexBytes = mf.size() < sizeof(d.mfHex) / 2 ? mf.size() : sizeof(d.mfHex) / 2;
  for (size_t i = 0; i < hexBytes; i++)
    snprintf(d.mfHex + i * 2, 3, "%02x", (uint8_t)mf[i]);

  if (mf.length() >= 13)
  {
    uint8_t dec     = (uint8_t)mf[10] & 0x0F;
    uint8_t tempInt = (uint8_t)mf[11] & 0x7F;
    bool    pos     = ((uint8_t)mf[11] & 0x80) != 0;
    d.humidity = (uint8_t)mf[12];
    d.temp     = tempInt + dec / 10.0f;
    if (!pos) d.temp = -d.temp;
    d.parsed = true;
  }
  if (sd.length() >= 3)
    d.battery = (uint8_t)sd[2];
}

ThermometerData ThermometerParser::parse(const char *addr, int8_t rssi,
                                          const std::string &mf,
                                          const std::string &sd)
{
  ThermometerData d = {};
  strncpy(d.address, addr, sizeof(d.address) - 1);
  d.rssi = rssi;
  parseCommon(d, mf, sd);
  return d;
}
