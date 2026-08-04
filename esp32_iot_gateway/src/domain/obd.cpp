#include "obd.h"

// 応答パケットの共通チェック: [0]=0x41(Mode01応答) [1]=PID [2]=A [3]=B ...（PCIバイトはcan.cpp側で剥離済み）
// 一致すればペイロード先頭（A）へのポインタを payload に返す
static bool checkHeader(const uint8_t *data, uint8_t dlc, uint8_t pid, uint8_t minDlc, const uint8_t *&payload)
{
  if (dlc < minDlc)
    return false;
  if (data[0] != 0x41 || data[1] != pid)
    return false;
  payload = data + 2;
  return true;
}

bool obdDecodeRpm(const uint8_t *data, uint8_t dlc, OBDReading &out)
{
  const uint8_t *p;
  if (!checkHeader(data, dlc, 0x0C, 4, p))
    return false;
  out.rpm = (uint16_t)(((uint16_t)p[0] * 256 + p[1]) / 4);
  return true;
}

bool obdDecodeSpeed(const uint8_t *data, uint8_t dlc, OBDReading &out)
{
  const uint8_t *p;
  if (!checkHeader(data, dlc, 0x0D, 3, p))
    return false;
  out.speed_kmh = p[0];
  return true;
}

bool obdDecodeLoad(const uint8_t *data, uint8_t dlc, OBDReading &out)
{
  const uint8_t *p;
  if (!checkHeader(data, dlc, 0x04, 3, p))
    return false;
  out.load_pct = (uint8_t)((uint16_t)p[0] * 100 / 255);
  return true;
}

bool obdDecodeMap(const uint8_t *data, uint8_t dlc, OBDReading &out)
{
  const uint8_t *p;
  if (!checkHeader(data, dlc, 0x0B, 3, p))
    return false;
  out.map_kpa = p[0];
  return true;
}

bool obdDecodeBaro(const uint8_t *data, uint8_t dlc, OBDReading &out)
{
  const uint8_t *p;
  if (!checkHeader(data, dlc, 0x33, 3, p))
    return false;
  out.baro_kpa = p[0];
  return true;
}

bool obdDecodeThrottle(const uint8_t *data, uint8_t dlc, OBDReading &out)
{
  const uint8_t *p;
  if (!checkHeader(data, dlc, 0x11, 3, p))
    return false;
  out.throttle_pct = (uint8_t)((uint16_t)p[0] * 100 / 255);
  return true;
}

bool obdDecodeTiming(const uint8_t *data, uint8_t dlc, OBDReading &out)
{
  const uint8_t *p;
  if (!checkHeader(data, dlc, 0x0E, 3, p))
    return false;
  out.timing_deg = p[0] / 2.0f - 64.0f;
  return true;
}

bool obdDecodeEcuVoltage(const uint8_t *data, uint8_t dlc, OBDReading &out)
{
  const uint8_t *p;
  if (!checkHeader(data, dlc, 0x42, 4, p))
    return false;
  out.ecu_voltage = ((uint16_t)p[0] * 256 + p[1]) / 1000.0f;
  return true;
}

bool obdDecodeMafAlt(const uint8_t *data, uint8_t dlc, OBDReading &out)
{
  const uint8_t *p;
  if (!checkHeader(data, dlc, 0x66, 5, p))
    return false;
  out.maf_gs = ((uint16_t)p[1] * 256 + p[2]) / 32.0f;
  return true;
}

bool obdDecodeCoolantAlt(const uint8_t *data, uint8_t dlc, OBDReading &out)
{
  const uint8_t *p;
  if (!checkHeader(data, dlc, 0x67, 4, p))
    return false;
  out.coolant_c = (int16_t)p[1] - 40;
  return true;
}

bool obdDecodeShortTermFuelTrim(const uint8_t *data, uint8_t dlc, OBDReading &out)
{
  const uint8_t *p;
  if (!checkHeader(data, dlc, 0x06, 3, p))
    return false;
  out.stft_pct = ((int)p[0] - 128) * 100.0f / 128.0f;
  return true;
}

bool obdDecodeLongTermFuelTrim(const uint8_t *data, uint8_t dlc, OBDReading &out)
{
  const uint8_t *p;
  if (!checkHeader(data, dlc, 0x07, 3, p))
    return false;
  out.ltft_pct = ((int)p[0] - 128) * 100.0f / 128.0f;
  return true;
}

bool obdDecodeO2SensorB1S2(const uint8_t *data, uint8_t dlc, OBDReading &out)
{
  const uint8_t *p;
  if (!checkHeader(data, dlc, 0x15, 4, p))
    return false;
  out.o2_b1s2_v = p[0] / 200.0f;
  out.o2_b1s2_trim_pct = ((int)p[1] - 128) * 100.0f / 128.0f;
  return true;
}

bool obdDecodeEngineRunTime(const uint8_t *data, uint8_t dlc, OBDReading &out)
{
  const uint8_t *p;
  if (!checkHeader(data, dlc, 0x1F, 4, p))
    return false;
  out.engine_run_time_sec = (uint16_t)p[0] * 256 + p[1];
  return true;
}

bool obdDecodeMilDistance(const uint8_t *data, uint8_t dlc, OBDReading &out)
{
  const uint8_t *p;
  if (!checkHeader(data, dlc, 0x21, 4, p))
    return false;
  out.mil_distance_km = (uint16_t)p[0] * 256 + p[1];
  return true;
}

bool obdDecodeO2Sensor1WideBand(const uint8_t *data, uint8_t dlc, OBDReading &out)
{
  const uint8_t *p;
  if (!checkHeader(data, dlc, 0x24, 6, p))
    return false;
  out.o2_s1_ratio = ((uint16_t)p[0] * 256 + p[1]) * 2.0f / 65536.0f;
  out.o2_s1_voltage = ((uint16_t)p[2] * 256 + p[3]) * 8.0f / 65536.0f;
  return true;
}

bool obdDecodeEvapPurge(const uint8_t *data, uint8_t dlc, OBDReading &out)
{
  const uint8_t *p;
  if (!checkHeader(data, dlc, 0x2E, 3, p))
    return false;
  out.evap_purge_pct = (uint8_t)((uint16_t)p[0] * 100 / 255);
  return true;
}

bool obdDecodeWarmupsSinceCleared(const uint8_t *data, uint8_t dlc, OBDReading &out)
{
  const uint8_t *p;
  if (!checkHeader(data, dlc, 0x30, 3, p))
    return false;
  out.warmups_since_cleared = p[0];
  return true;
}

bool obdDecodeDistanceSinceCleared(const uint8_t *data, uint8_t dlc, OBDReading &out)
{
  const uint8_t *p;
  if (!checkHeader(data, dlc, 0x31, 4, p))
    return false;
  out.distance_since_cleared_km = (uint16_t)p[0] * 256 + p[1];
  return true;
}

bool obdDecodeCatalystTemp(const uint8_t *data, uint8_t dlc, OBDReading &out)
{
  const uint8_t *p;
  if (!checkHeader(data, dlc, 0x3C, 4, p))
    return false;
  out.catalyst_temp_c = ((uint16_t)p[0] * 256 + p[1]) / 10.0f - 40.0f;
  return true;
}

bool obdDecodeAbsoluteLoad(const uint8_t *data, uint8_t dlc, OBDReading &out)
{
  const uint8_t *p;
  if (!checkHeader(data, dlc, 0x43, 4, p))
    return false;
  out.absolute_load_pct = ((uint16_t)p[0] * 256 + p[1]) * 100.0f / 255.0f;
  return true;
}

bool obdDecodeCommandedAfr(const uint8_t *data, uint8_t dlc, OBDReading &out)
{
  const uint8_t *p;
  if (!checkHeader(data, dlc, 0x44, 4, p))
    return false;
  out.commanded_afr = ((uint16_t)p[0] * 256 + p[1]) * 2.0f / 65536.0f;
  return true;
}

bool obdDecodeThrottleB(const uint8_t *data, uint8_t dlc, OBDReading &out)
{
  const uint8_t *p;
  if (!checkHeader(data, dlc, 0x47, 3, p))
    return false;
  out.throttle_b_pct = (uint8_t)((uint16_t)p[0] * 100 / 255);
  return true;
}

bool obdDecodeAccelPedalD(const uint8_t *data, uint8_t dlc, OBDReading &out)
{
  const uint8_t *p;
  if (!checkHeader(data, dlc, 0x49, 3, p))
    return false;
  out.accel_pedal_d_pct = (uint8_t)((uint16_t)p[0] * 100 / 255);
  return true;
}

bool obdDecodeAccelPedalE(const uint8_t *data, uint8_t dlc, OBDReading &out)
{
  const uint8_t *p;
  if (!checkHeader(data, dlc, 0x4A, 3, p))
    return false;
  out.accel_pedal_e_pct = (uint8_t)((uint16_t)p[0] * 100 / 255);
  return true;
}

bool obdDecodeFuelType(const uint8_t *data, uint8_t dlc, OBDReading &out)
{
  const uint8_t *p;
  if (!checkHeader(data, dlc, 0x51, 3, p))
    return false;
  out.fuel_type = p[0];
  return true;
}

bool obdDecodeSecO2TrimShortTerm(const uint8_t *data, uint8_t dlc, OBDReading &out)
{
  const uint8_t *p;
  if (!checkHeader(data, dlc, 0x55, 3, p))
    return false;
  out.sec_o2_trim_st_pct = ((int)p[0] - 128) * 100.0f / 128.0f;
  return true;
}

bool obdDecodeSecO2TrimLongTerm(const uint8_t *data, uint8_t dlc, OBDReading &out)
{
  const uint8_t *p;
  if (!checkHeader(data, dlc, 0x56, 3, p))
    return false;
  out.sec_o2_trim_lt_pct = ((int)p[0] - 128) * 100.0f / 128.0f;
  return true;
}

void obdReadingToBlePacket(const OBDReading &r, ObdBlePacket &out)
{
  out.rpm = r.rpm;
  out.speed_kmh = r.speed_kmh;
  out.load_pct = r.load_pct;
  out.map_kpa = r.map_kpa;
  out.baro_kpa = r.baro_kpa;
  out.boost_kpa = r.boost_kpa;
  out.throttle_pct = r.throttle_pct;
  out.timing_deg = r.timing_deg;
  out.ecu_voltage = r.ecu_voltage;
  out.maf_gs = r.maf_gs;
  out.coolant_c = r.coolant_c;
  out.fuel_rate_lph = r.fuel_rate_lph;

  out.stft_pct = r.stft_pct;
  out.ltft_pct = r.ltft_pct;
  out.o2_b1s2_v = r.o2_b1s2_v;
  out.o2_b1s2_trim_pct = r.o2_b1s2_trim_pct;
  out.engine_run_time_sec = r.engine_run_time_sec;
  out.mil_distance_km = r.mil_distance_km;
  out.o2_s1_ratio = r.o2_s1_ratio;
  out.o2_s1_voltage = r.o2_s1_voltage;
  out.evap_purge_pct = r.evap_purge_pct;
  out.warmups_since_cleared = r.warmups_since_cleared;
  out.distance_since_cleared_km = r.distance_since_cleared_km;
  out.catalyst_temp_c = r.catalyst_temp_c;
  out.absolute_load_pct = r.absolute_load_pct;
  out.commanded_afr = r.commanded_afr;
  out.throttle_b_pct = r.throttle_b_pct;
  out.accel_pedal_d_pct = r.accel_pedal_d_pct;
  out.accel_pedal_e_pct = r.accel_pedal_e_pct;
  out.fuel_type = r.fuel_type;
  out.sec_o2_trim_st_pct = r.sec_o2_trim_st_pct;
  out.sec_o2_trim_lt_pct = r.sec_o2_trim_lt_pct;

  out.valid = r.valid ? 1 : 0;
  out.ts = (uint32_t)r.ts;
}
