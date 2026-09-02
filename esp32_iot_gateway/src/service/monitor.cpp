#include "monitor.h"
#include "pubqueue.h"
#include "../logger.h"
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

  // 進行中のスキャンがなければキュークリア → 非同期スキャン開始
  // （既にスキャン中なら二重startせず、進行中のスキャンをそのまま使う）
  if (!bleScanner.isScanning())
  {
    SensorVariant dummy;
    while (xQueueReceive(bleScanner.queue, &dummy, 0) == pdTRUE)
    {
    }
    bleScanner.startAsync(SCAN_TIME);
  }

  MeasureResult result;

  // アナログ計測
  result.reading = {
      {adsReadDiffMain()},
      {adsReadDiffSub()},
      {ina228.readCurrent(), ina228.readPower(), ina228.readTemp(), ina228.readCharge() + (float)getAhOffset()},
      time(nullptr)};

  result.bleCount = 0; // BLEはまだ未収集（スキャン完了後にcollectBle()で収集する）

  return result;
}

bool collectBle(MeasureResult &result)
{
  if (bleScanner.isScanning())
    return false; // まだスキャン中

  result.bleCount = 0;
  while (result.bleCount < QUEUE_SIZE &&
         xQueueReceive(bleScanner.queue, &result.ble[result.bleCount], 0) == pdTRUE)
  {
    result.bleCount++;
  }
  return true;
}

void publishBattery(const SensorReading &r)
{
  logger.printf("[MONITOR] main=%.2fV sub=%.2fV cur=%.4fA pwr=%.3fW tmp=%.1fC ah=%.6fAh ts=%lld\n",
                r.main.voltage, r.sub.voltage, r.pwr.current, r.pwr.power, r.pwr.temp, r.pwr.ah,
                (long long)r.ts);

  queue.pushBattery(r);
}

void publishBle(const MeasureResult &result)
{
  logger.printf("[MONITOR] ble=%d\n", result.bleCount);

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
