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

  // 追加確定PID（OBD.md「確定した取得可能データ一覧」参照）
  float    stft_pct;                 // 0x06: (A-128)*100/128 [%]
  float    ltft_pct;                 // 0x07: (A-128)*100/128 [%]
  float    o2_b1s2_v;                // 0x15: A/200 [V]
  float    o2_b1s2_trim_pct;         // 0x15: (B-128)*100/128 [%]
  uint16_t engine_run_time_sec;      // 0x1F: A*256+B [秒]
  uint16_t mil_distance_km;          // 0x21: A*256+B [km]
  float    o2_s1_ratio;              // 0x24: (A*256+B)*2/65536
  float    o2_s1_voltage;            // 0x24: (C*256+D)*8/65536 [V]
  uint8_t  evap_purge_pct;           // 0x2E: A*100/255 [%]
  uint8_t  warmups_since_cleared;    // 0x30: A [回]
  uint16_t distance_since_cleared_km;// 0x31: A*256+B [km]
  float    catalyst_temp_c;          // 0x3C: (A*256+B)/10-40 [°C]
  float    absolute_load_pct;        // 0x43: (A*256+B)*100/255 [%]
  float    commanded_afr;            // 0x44: (A*256+B)*2/65536
  uint8_t  throttle_b_pct;           // 0x47: A*100/255 [%]
  uint8_t  accel_pedal_d_pct;        // 0x49: A*100/255 [%]
  uint8_t  accel_pedal_e_pct;        // 0x4A: A*100/255 [%]
  uint8_t  fuel_type;                // 0x51: A（1=ガソリン）
  float    sec_o2_trim_st_pct;       // 0x55: (A-128)*100/128 [%]
  float    sec_o2_trim_lt_pct;       // 0x56: (A-128)*100/128 [%]

  bool     valid;
  time_t   ts;

  // 末尾追加（ObdBlePacketとのオフセット互換のため既存フィールドより後ろに置く）
  int16_t  iat_c;  // 0x68 Sensor1: B-40 [°C]（インタークーラー前後どちらか未確定）
  int16_t  iat2_c; // 0x68 Sensor2: C-40 [°C]（同上）
};

// BLE Notify 送信用（パディングなしで詰めた固定レイアウト）。
// OBDReading をそのまま memcpy するとコンパイラのパディング/アライメントに依存してしまうため、
// BLE経由で送る際はこの構造体に変換してから使う（device/ble_peripheral.cpp 参照）。
#pragma pack(push, 1)
struct ObdBlePacket
{
  uint16_t rpm;
  uint8_t  speed_kmh;
  uint8_t  load_pct;
  uint8_t  map_kpa;
  uint8_t  baro_kpa;
  int8_t   boost_kpa;
  uint8_t  throttle_pct;
  float    timing_deg;
  float    ecu_voltage;
  float    maf_gs;
  int16_t  coolant_c;
  float    fuel_rate_lph;

  float    stft_pct;
  float    ltft_pct;
  float    o2_b1s2_v;
  float    o2_b1s2_trim_pct;
  uint16_t engine_run_time_sec;
  uint16_t mil_distance_km;
  float    o2_s1_ratio;
  float    o2_s1_voltage;
  uint8_t  evap_purge_pct;
  uint8_t  warmups_since_cleared;
  uint16_t distance_since_cleared_km;
  float    catalyst_temp_c;
  float    absolute_load_pct;
  float    commanded_afr;
  uint8_t  throttle_b_pct;
  uint8_t  accel_pedal_d_pct;
  uint8_t  accel_pedal_e_pct;
  uint8_t  fuel_type;
  float    sec_o2_trim_st_pct;
  float    sec_o2_trim_lt_pct;

  uint8_t  valid; // bool を1バイト固定で送る
  uint32_t ts;    // time_t は環境依存サイズのため uint32_t に固定

  int16_t  iat_c;
  int16_t  iat2_c;
};
#pragma pack(pop)

// OBDReading → ObdBlePacket 変換
void obdReadingToBlePacket(const OBDReading &r, ObdBlePacket &out);

// デコード共通ルール: data[0]!=0x41 または data[1]!=要求PID または dlc不足の場合は false を返す
// （data は can.cpp 側で ISO-TP PCI バイトを剥がし済み。data[2]以降がA,B,C...のペイロード）

bool obdDecodeRpm(const uint8_t *data, uint8_t dlc, OBDReading &out);
bool obdDecodeSpeed(const uint8_t *data, uint8_t dlc, OBDReading &out);
bool obdDecodeLoad(const uint8_t *data, uint8_t dlc, OBDReading &out);
bool obdDecodeMap(const uint8_t *data, uint8_t dlc, OBDReading &out);
bool obdDecodeBaro(const uint8_t *data, uint8_t dlc, OBDReading &out);
bool obdDecodeThrottle(const uint8_t *data, uint8_t dlc, OBDReading &out);
bool obdDecodeTiming(const uint8_t *data, uint8_t dlc, OBDReading &out);
bool obdDecodeEcuVoltage(const uint8_t *data, uint8_t dlc, OBDReading &out);

// PID 0x66: sensors=data[2]（N-VANでは0x01）, MAF=(data[3]*256+data[4])/32 g/s
bool obdDecodeMafAlt(const uint8_t *data, uint8_t dlc, OBDReading &out);

// PID 0x67: bitmap=data[2]（0x03=S1+S2）, Sensor1温度=data[3]-40 °C
bool obdDecodeCoolantAlt(const uint8_t *data, uint8_t dlc, OBDReading &out);

// 追加確定PID（OBD.md「確定した取得可能データ一覧」参照）
bool obdDecodeShortTermFuelTrim(const uint8_t *data, uint8_t dlc, OBDReading &out);   // 0x06
bool obdDecodeLongTermFuelTrim(const uint8_t *data, uint8_t dlc, OBDReading &out);    // 0x07
bool obdDecodeO2SensorB1S2(const uint8_t *data, uint8_t dlc, OBDReading &out);        // 0x15（電圧+燃料トリムの2値）
bool obdDecodeEngineRunTime(const uint8_t *data, uint8_t dlc, OBDReading &out);       // 0x1F
bool obdDecodeMilDistance(const uint8_t *data, uint8_t dlc, OBDReading &out);         // 0x21
bool obdDecodeO2Sensor1WideBand(const uint8_t *data, uint8_t dlc, OBDReading &out);   // 0x24（ratio+電圧の2値）
bool obdDecodeEvapPurge(const uint8_t *data, uint8_t dlc, OBDReading &out);           // 0x2E
bool obdDecodeWarmupsSinceCleared(const uint8_t *data, uint8_t dlc, OBDReading &out); // 0x30
bool obdDecodeDistanceSinceCleared(const uint8_t *data, uint8_t dlc, OBDReading &out);// 0x31
bool obdDecodeCatalystTemp(const uint8_t *data, uint8_t dlc, OBDReading &out);        // 0x3C
bool obdDecodeAbsoluteLoad(const uint8_t *data, uint8_t dlc, OBDReading &out);        // 0x43
bool obdDecodeCommandedAfr(const uint8_t *data, uint8_t dlc, OBDReading &out);        // 0x44
bool obdDecodeThrottleB(const uint8_t *data, uint8_t dlc, OBDReading &out);           // 0x47
bool obdDecodeAccelPedalD(const uint8_t *data, uint8_t dlc, OBDReading &out);         // 0x49
bool obdDecodeAccelPedalE(const uint8_t *data, uint8_t dlc, OBDReading &out);         // 0x4A
bool obdDecodeFuelType(const uint8_t *data, uint8_t dlc, OBDReading &out);            // 0x51
bool obdDecodeSecO2TrimShortTerm(const uint8_t *data, uint8_t dlc, OBDReading &out);  // 0x55
bool obdDecodeSecO2TrimLongTerm(const uint8_t *data, uint8_t dlc, OBDReading &out);   // 0x56

// PID 0x68: bitmap=data[2]（0x03=S1+S2）, Sensor1温度=data[3]-40 [°C], Sensor2温度=data[4]-40 [°C]
// （インタークーラー前後どちらがSensor1/2に対応するかは未確定）
bool obdDecodeChargeAirTemp(const uint8_t *data, uint8_t dlc, OBDReading &out);       // 0x68
