#include "monitor.h"
#include "pubqueue.h"
#include "logger.h"
#include "../device/ads.h"
#include "../device/ina228.h"
#include "../device/ble_scan.h"
#include "../device/oled.h"
#include "../config.h"
#include "../domain/ble_targets.h"
#include <freertos/queue.h>
#include <time.h>

MeasureResult measure()
{
  oledShowMessage("BLE Scanning...", "(10 sec)");

  // キュークリア → スキャン
  SensorVariant dummy;
  while (xQueueReceive(bleScanner.queue, &dummy, 0) == pdTRUE)
  {
  }
  bleScanner.start(SCAN_TIME);

  MeasureResult result;

  // アナログ計測
  result.reading = {
      {adsReadDiff01()},
      {adsReadDiff23()},
      {ina228.readCurrent(), ina228.readPower(), ina228.readTemp()},
      time(nullptr)};

  // BLE キューを全件収集
  result.bleCount = 0;
  while (result.bleCount < QUEUE_SIZE &&
         xQueueReceive(bleScanner.queue, &result.ble[result.bleCount], 0) == pdTRUE)
  {
    result.bleCount++;
  }

  return result;
}

void publish(const MeasureResult &result)
{
  const SensorReading &r = result.reading;

  logger.printf("[MONITOR] v1=%.2fV v2=%.2fV cur=%.4fA pwr=%.3fW tmp=%.1fC ts=%lld ble=%d\n",
                r.v1.voltage, r.v2.voltage, r.pwr.current, r.pwr.power, r.pwr.temp,
                (long long)r.ts, result.bleCount);

  queue.pushShadow(r);

  for (int i = 0; i < result.bleCount; i++)
  {
    std::visit([&](auto &d)
               {
      using T = std::decay_t<decltype(d)>;
      if (!bleTargets.isTarget(d.address)) return;
      if constexpr (std::is_same_v<T, ThermometerData>) {
        queue.pushThermometer(d);
      } else if constexpr (std::is_same_v<T, Co2MeterData>) {
        queue.pushCo2(d);
      } }, result.ble[i]);
  }
}
