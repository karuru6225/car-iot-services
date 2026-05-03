#include "monitor.h"
#include "mqtt.h"
#include "logger.h"
#include "../device/ads.h"
#include "../device/ina228.h"
#include "../device/ble_scan.h"
#include "../device/oled.h"
#include "../domain/telemetry.h"
#include "../domain/measurement.h"
#include "../domain/sensor_factory.h"
#include "../config.h"
#include <freertos/queue.h>
#include <time.h>

void measureAndPublish() {
  // BLE スキャン
  oledShowMessage("BLE Scanning...", "(10 sec)");
  SensorVariant dummy;
  while (xQueueReceive(bleScanner.queue, &dummy, 0) == pdTRUE) {}
  bleScanner.start(SCAN_TIME);

  // 計測
  VoltageReading v1{adsReadDiff01()};
  VoltageReading v2{adsReadDiff23()};
  PowerReading pwr{ina228ReadCurrent(), ina228ReadPower(), ina228ReadTemp()};
  time_t ts = time(nullptr);

  logger.printf("[MONITOR] v1=%.2fV v2=%.2fV cur=%.4fA pwr=%.3fW tmp=%.1fC ts=%lld\n",
                v1.voltage, v2.voltage, pwr.current, pwr.power, pwr.temp, (long long)ts);

  // Shadow publish
  char shadowTopic[80];
  snprintf(shadowTopic, sizeof(shadowTopic), "$aws/things/%s/shadow/update", getDeviceId());
  char shadowBuf[256];
  buildShadowPayload(shadowBuf, sizeof(shadowBuf), v1, v2, pwr, ts);
  if (!mqtt.publish(shadowTopic, shadowBuf)) {
    logger.println("[MONITOR] Shadow publish failed");
  }

  // BLE センサー publish
  char sensorTopic[80];
  snprintf(sensorTopic, sizeof(sensorTopic), "sensors/%s/data", getDeviceId());
  char sensorBuf[PAYLOAD_SENSOR_SIZE];
  SensorVariant sv;
  while (xQueueReceive(bleScanner.queue, &sv, 0) == pdTRUE) {
    int len = 0;
    std::visit([&](auto &d) {
      using T = std::decay_t<decltype(d)>;
      if constexpr (std::is_same_v<T, ThermometerData>) {
        len = buildThermometerPayload(sensorBuf, sizeof(sensorBuf), d, "");
      } else if constexpr (std::is_same_v<T, Co2MeterData>) {
        len = buildCo2Payload(sensorBuf, sizeof(sensorBuf), d, "");
      }
    }, sv);
    if (len > 0 && !mqtt.publish(sensorTopic, sensorBuf)) {
      logger.println("[MONITOR] Sensor publish failed");
    }
  }

  // OLED 更新
  oledShowSensorData(v1.voltage, v2.voltage, pwr.current, pwr.power, pwr.temp);
}
