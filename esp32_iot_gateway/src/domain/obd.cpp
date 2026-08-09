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
  if (!(p[0] & 0x01)) // Sensor1 MAF 非搭載
    return false;
  out.maf_gs = ((uint16_t)p[1] * 256 + p[2]) / 32.0f;
  return true;
}

bool obdDecodeCoolantAlt(const uint8_t *data, uint8_t dlc, OBDReading &out)
{
  const uint8_t *p;
  if (!checkHeader(data, dlc, 0x67, 4, p))
    return false;
  if (!(p[0] & 0x01)) // Sensor1 非搭載
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

bool obdDecodeChargeAirTemp(const uint8_t *data, uint8_t dlc, OBDReading &out)
{
  const uint8_t *p;
  if (!checkHeader(data, dlc, 0x68, 5, p))
    return false;
  bool any = false;
  if (p[0] & 0x01) // Sensor1 搭載
  {
    out.iat_c = (int16_t)p[1] - 40;
    any = true;
  }
  if (p[0] & 0x02) // Sensor2 搭載
  {
    out.iat2_c = (int16_t)p[2] - 40;
    any = true;
  }
  return any;
}

// PID→データ長（PIDバイト自身を除く、A/B/C...の合計バイト数）。多くは上の
// obdDecode*() の checkHeader() minDlc から算出（minDlc - 2）で正しいが、
// 0x66/0x67/0x68（マスクバイト+複数センサー枠を持つ拡張PID群）は
// デコーダが最初のセンサー分しか読まないため minDlc からは実際の応答長が分からない。
// この3件は実車の生応答（多PIDバッチ応答・0x68単発応答）から実測した値
// （HANDOFF_isotp_multipid.md参照。0x66=マスク+2センサー×2byte、
// 0x67=マスク+2センサー×1byte、0x68=マスク+6byte）。kPids（obdpoll.cpp）と
// 対応するPIDを追加したら、ここにも追記すること（実測せず minDlc から機械的に
// 決めると同様の齟齬が起きうるので注意）。
namespace
{
struct PidLength
{
  uint8_t pid;
  uint8_t len;
};

const PidLength kPidLengths[] = {
    {0x04, 1}, {0x06, 1}, {0x07, 1}, {0x0B, 1}, {0x0C, 2}, {0x0D, 1}, {0x0E, 1},
    {0x11, 1}, {0x15, 2}, {0x1F, 2}, {0x21, 2}, {0x24, 4}, {0x2E, 1}, {0x30, 1},
    {0x31, 2}, {0x33, 1}, {0x3C, 2}, {0x42, 2}, {0x43, 2}, {0x44, 2}, {0x47, 1},
    {0x49, 1}, {0x4A, 1}, {0x51, 1}, {0x55, 1}, {0x56, 1}, {0x66, 5}, {0x67, 3}, {0x68, 7},
};

bool lookupPidLength(uint8_t pid, uint8_t &lenOut)
{
  for (const auto &e : kPidLengths)
  {
    if (e.pid == pid)
    {
      lenOut = e.len;
      return true;
    }
  }
  return false;
}
} // namespace

void obdParseMultiResponse(const uint8_t *data, uint8_t dlc, ObdMultiSegmentCb cb, void *ctx)
{
  if (dlc < 1 || data[0] != 0x41)
    return;

  uint8_t i = 1;
  while (i < dlc)
  {
    uint8_t pid = data[i++];
    uint8_t len;
    if (!lookupPidLength(pid, len))
      break; // 未知PID: 以降のセグメント境界が分からないため打ち切り
    if ((uint16_t)i + len > dlc)
      break; // データ不足（応答が途中で切れている）
    cb(pid, data + i, len, ctx);
    i += len;
  }
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

  out.iat_c = r.iat_c;
  out.iat2_c = r.iat2_c;
}

void obdComputeDerived(OBDReading &r)
{
  if (!r.valid)
    return;

  // boost = MAP - Baro（Baro未取得時は標準大気圧101kPaで代用）
  r.boost_kpa = (int8_t)(r.map_kpa - (r.baro_kpa > 0 ? r.baro_kpa : 101));
  // 燃費推算（MAFから）: OBD.md 参照
  if (r.maf_gs > 0)
    r.fuel_rate_lph = r.maf_gs / (14.7f * 0.745f) * 3.6f;
}
