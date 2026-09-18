#pragma once
#include <stddef.h>
#include <stdint.h>
#include <time.h>
#include <optional>
#include <ArduinoJson.h>
#include "measurement.h"
#include "thermometer.h"
#include "co2meter.h"

// buildConfigPayload() の出力を受けるバッファサイズ。最長ケース（desired:null付き・
// override_next_mode/continuous_until_time/default_mode すべて非null）で約300バイト。
// 以前の256バイトでは default_mode 追加後に desired:null 付きペイロードが溢れ、壊れたJSONを
// 送って desired のクリアが失敗し続けていた。reportedへフィールドを足したら
// test_telemetry.cpp の最長ケーステストで収まることを確認すること
static const size_t CONFIG_PAYLOAD_SIZE = 512;

// Shadow reported 向けデバイス設定ペイロードを組み立てる
// clearDesired=true のとき "desired":null を付加して desired をクリアする
// overrideNextMode: "timed_continuous" を渡すと ACK として報告、nullptr で null 報告（通常時）
// continuousUntilTime: TIMED_CONTINUOUS中の継続期限（絶対UNIX時刻）。std::nulloptでnull報告
// defaultMode: NVSに設定済みのデフォルトモード名（"light_sleep"等）。nullptrでnull報告（未設定時）
// 戻り値はsnprintfと同じく「切り詰めなしで必要だった長さ」。size以上なら切り詰められている
int buildConfigPayload(char *buf, size_t size, bool clearDesired = false,
                       const char *overrideNextMode = nullptr,
                       std::optional<time_t> continuousUntilTime = std::nullopt,
                       const char *defaultMode = nullptr);

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
