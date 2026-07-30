#pragma once
#include <stdint.h>
#include <time.h>

// Honda N-VAN で実車確認済みの OBD-II Mode 01 PID を扱う（OBD.md 参照）
struct OBDReading
{
  uint16_t rpm;           // 0x0C: (A*256+B)/4 [rpm]
  uint8_t  speed_kmh;     // 0x0D: A [km/h]
  uint8_t  load_pct;      // 0x04: A*100/255 [%]
  uint8_t  map_kpa;       // 0x0B: A [kPa 絶対圧]
  uint8_t  baro_kpa;      // 0x33: A [kPa]
  int8_t   boost_kpa;     // map_kpa - baro_kpa [kPa]（呼び出し側で計算）
  uint8_t  throttle_pct;  // 0x11: A*100/255 [%]
  float    timing_deg;    // 0x0E: A/2.0-64.0 [°BTDC]
  float    ecu_voltage;   // 0x42: (A*256+B)/1000.0 [V]
  float    maf_gs;        // 0x66: (B*256+C)/32 [g/s]（0x10 MAF 非対応のため代替）
  int16_t  coolant_c;     // 0x67 Sensor1: B-40 [°C]（0x05 水温 非対応のため代替）
  float    fuel_rate_lph; // MAF 推算: maf_gs / (14.7*0.745) * 3.6 [L/h]（呼び出し側で計算）

  bool     valid;
  time_t   ts;
};

// デコード共通ルール: data[1]!=0x41 または data[2]!=要求PID または dlc不足の場合は false を返す

bool obdDecodeRpm(const uint8_t *data, uint8_t dlc, OBDReading &out);
bool obdDecodeSpeed(const uint8_t *data, uint8_t dlc, OBDReading &out);
bool obdDecodeLoad(const uint8_t *data, uint8_t dlc, OBDReading &out);
bool obdDecodeMap(const uint8_t *data, uint8_t dlc, OBDReading &out);
bool obdDecodeBaro(const uint8_t *data, uint8_t dlc, OBDReading &out);
bool obdDecodeThrottle(const uint8_t *data, uint8_t dlc, OBDReading &out);
bool obdDecodeTiming(const uint8_t *data, uint8_t dlc, OBDReading &out);
bool obdDecodeEcuVoltage(const uint8_t *data, uint8_t dlc, OBDReading &out);

// PID 0x66: sensors=data[3]（N-VANでは0x01）, MAF=(data[4]*256+data[5])/32 g/s
bool obdDecodeMafAlt(const uint8_t *data, uint8_t dlc, OBDReading &out);

// PID 0x67: bitmap=data[3]（0x03=S1+S2）, Sensor1温度=data[4]-40 °C
bool obdDecodeCoolantAlt(const uint8_t *data, uint8_t dlc, OBDReading &out);
