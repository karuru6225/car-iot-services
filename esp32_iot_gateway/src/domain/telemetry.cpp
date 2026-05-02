#include "telemetry.h"
#include <stdio.h>

int buildShadowPayload(char *buf, size_t size,
                       const VoltageReading &v1,
                       const VoltageReading &v2,
                       const PowerReading &pwr,
                       time_t ts)
{
  return snprintf(buf, size,
                  "{\"state\":{\"reported\":{"
                  "\"v1\":%.2f,"
                  "\"v2\":%.2f,"
                  "\"current\":%.4f,"
                  "\"power\":%.3f,"
                  "\"temp\":%.1f,"
                  "\"ts\":%lld"
                  "}}}",
                  v1.voltage, v2.voltage,
                  pwr.current, pwr.power, pwr.temp,
                  (long long)ts);
}
