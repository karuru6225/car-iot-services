#include "telemetry.h"
#include <stdio.h>
#include "../config.h"

int buildShadowPayload(char *buf, size_t size,
                       const VoltageReading &main,
                       const VoltageReading &sub,
                       const PowerReading &pwr,
                       time_t ts)
{
  return snprintf(buf, size,
                  "{\"state\":{\"reported\":{"
                  "\"main\":%.2f,"
                  "\"sub\":%.2f,"
                  "\"current\":%.4f,"
                  "\"power\":%.3f,"
                  "\"temp\":%.1f,"
                  "\"ah\":%.6f,"
                  "\"ts\":%lld"
                  "}}}",
                  main.voltage, sub.voltage,
                  pwr.current, pwr.power, pwr.temp, pwr.ah,
                  (long long)ts);
}

int buildThermometerPayload(char *buf, size_t size,
                             const ThermometerData &d,
                             const char *tsField)
{
  return snprintf(buf, size,
                  "{\"type\":\"thermometer\","
                  "\"addr\":\"%s\","
                  "\"temp\":%.1f,"
                  "\"humidity\":%d,"
                  "\"battery\":%d,"
                  "\"rssi\":%d,"
                  "\"mf\":\"%s\","
                  "\"fw\":\"" FIRMWARE_VERSION "\"%s}",
                  d.address, d.temp, d.humidity, d.battery, d.rssi, d.mfHex, tsField);
}

int buildCo2Payload(char *buf, size_t size,
                    const Co2MeterData &d,
                    const char *tsField)
{
  return snprintf(buf, size,
                  "{\"type\":\"co2meter\","
                  "\"addr\":\"%s\","
                  "\"temp\":%.1f,"
                  "\"humidity\":%d,"
                  "\"co2\":%d,"
                  "\"battery\":%d,"
                  "\"rssi\":%d,"
                  "\"mf\":\"%s\","
                  "\"fw\":\"" FIRMWARE_VERSION "\"%s}",
                  d.address, d.temp, d.humidity, d.co2, d.battery, d.rssi, d.mfHex, tsField);
}
