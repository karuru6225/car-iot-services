#pragma once
#include <stddef.h>
#include <stdint.h>
#include <time.h>
#include <ArduinoJson.h>
#include "measurement.h"
#include "thermometer.h"
#include "co2meter.h"

// Shadow reported 向けデバイス設定ペイロードを組み立てる
// clearDesired=true のとき "desired":null を付加して desired をクリアする
int buildConfigPayload(char *buf, size_t size, bool clearDesired = false);

// ─── テレメトリエンコーダ（pubqueue が使用） ────────────────────────────────────
//
// encode* は基底クラスが実装（ドキュメント組み立て → serialize 呼び出し）。
// 派生クラスは serialize と topicSuffix のみ実装する。

class ITelemetryEncoder {
public:
  virtual ~ITelemetryEncoder() = default;

  size_t encodeBattery(uint8_t *buf, size_t cap,
                       const VoltageReading &main, const VoltageReading &sub,
                       const PowerReading &pwr, time_t ts);
  size_t encodeThermometer(uint8_t *buf, size_t cap, const ThermometerData &d);
  size_t encodeCo2(uint8_t *buf, size_t cap, const Co2MeterData &d);

  virtual const char *topicSuffix() const = 0;

protected:
  virtual size_t serialize(JsonDocument &doc, uint8_t *buf, size_t cap) = 0;
};

class JsonTelemetryEncoder : public ITelemetryEncoder {
public:
  const char *topicSuffix() const override { return "data"; }
protected:
  size_t serialize(JsonDocument &doc, uint8_t *buf, size_t cap) override;
};

class MsgPackTelemetryEncoder : public ITelemetryEncoder {
public:
  const char *topicSuffix() const override { return "data_bin"; }
protected:
  size_t serialize(JsonDocument &doc, uint8_t *buf, size_t cap) override;
};
