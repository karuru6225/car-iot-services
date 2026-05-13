#pragma once
#include <stdint.h>
#include "../domain/measurement.h"
#include "../domain/thermometer.h"
#include "../domain/co2meter.h"

enum class EntryType : uint8_t { Shadow = 0, Thermometer = 1, Co2 = 2 };

struct ShadowEntry {
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
    ShadowEntry      shadow;
    ThermometerEntry thermo;
    Co2Entry         co2;
  };
};

class PubQueue {
public:
  explicit PubQueue(bool useSpiffs = true);

  // 計測値をキューに積む
  void pushShadow(const SensorReading &r);
  void pushThermometer(const ThermometerData &d);
  void pushCo2(const Co2MeterData &d);

  // LTE 接続中であればキューを MQTT へ送出する
  void flush();

  // 電源投入時: SPIFFS → RTC メモリ（DeepSleep 復帰時は何もしない）
  void load();

  // DeepSleep 前: RTC メモリ → SPIFFS（空なら SPIFFS ファイルを削除）
  // useSpiffs=false の場合は何もしない
  void save();

  int  size()  const;
  bool empty() const;

private:
  void push(const QueueEntry &e);
  bool _useSpiffs;
};

extern PubQueue queue;
