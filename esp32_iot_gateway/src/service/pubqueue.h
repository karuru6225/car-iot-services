#pragma once
#include <stdint.h>
#include "../domain/measurement.h"
#include "../domain/thermometer.h"
#include "../domain/co2meter.h"
#include "../domain/telemetry.h"
#include "../domain/diag.h"

enum class EntryType : uint8_t { Battery = 0, Thermometer = 1, Co2 = 2, Diag = 3 };

struct BatteryEntry {
  float    main, sub, current, power, temp, ah;
  uint32_t ts;
};

struct ThermometerEntry {
  uint8_t addr[6];
  int8_t  rssi;
  int16_t temp;      // ×10 固定小数点
  uint8_t humidity;
  uint8_t battery;
};

struct Co2Entry {
  uint8_t  addr[6];
  int8_t   rssi;
  int16_t  temp;     // ×10 固定小数点
  uint8_t  humidity;
  uint16_t co2;
  uint8_t  battery;
};

struct QueueEntry {
  EntryType type;
  union {
    BatteryEntry     battery;
    ThermometerEntry thermo;
    Co2Entry         co2;
    DiagData         diag;
  };
};

// QueueEntry は SPIFFS(/buffer.bin) に生バイト列で保存される。union が BatteryEntry より
// 大きくなると sizeof(QueueEntry) が変わり、OTA直後に旧形式の保存ファイルを読み違える
static_assert(sizeof(DiagData) <= sizeof(BatteryEntry),
              "DiagData must not grow QueueEntry (breaks /buffer.bin compatibility)");

class PubQueue {
public:
  explicit PubQueue(bool useSpiffs = true);

  // エンコーダを設定する（load() より前に呼ぶこと）
  void setEncoder(ITelemetryEncoder *enc);

  // 計測値をキューに積む
  void pushBattery(const SensorReading &r);
  void pushThermometer(const ThermometerData &d);
  void pushCo2(const Co2MeterData &d);
  void pushDiag(const DiagData &d);

  // LTE 接続中であればキューを MQTT へ送出する
  void flush();

  // 電源投入時: SPIFFS → RTC メモリ（DeepSleep 復帰時は何もしない）
  void load();

  // DeepSleep 前: RTC メモリ → SPIFFS（空なら SPIFFS ファイルを削除）
  // useSpiffs=false の場合は何もしない
  void save();

  int  size()  const;
  bool empty() const;

  // キュー溢れで捨てた件数の累計（RTCメモリ保持。電源投入・ブラウンアウト等で0に戻る）
  uint16_t droppedCount() const;

private:
  void push(const QueueEntry &e);
  ITelemetryEncoder *_encoder = nullptr;
  bool _useSpiffs;
};

extern PubQueue queue;
