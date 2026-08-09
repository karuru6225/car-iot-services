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
  out.speedKmh = p[0];
  return true;
}

bool obdDecodeLoad(const uint8_t *data, uint8_t dlc, OBDReading &out)
{
  const uint8_t *p;
  if (!checkHeader(data, dlc, 0x04, 3, p))
    return false;
  out.loadPct = (uint8_t)((uint16_t)p[0] * 100 / 255);
  return true;
}

bool obdDecodeMap(const uint8_t *data, uint8_t dlc, OBDReading &out)
{
  const uint8_t *p;
  if (!checkHeader(data, dlc, 0x0B, 3, p))
    return false;
  out.mapKpa = p[0];
  return true;
}

bool obdDecodeBaro(const uint8_t *data, uint8_t dlc, OBDReading &out)
{
  const uint8_t *p;
  if (!checkHeader(data, dlc, 0x33, 3, p))
    return false;
  out.baroKpa = p[0];
  return true;
}

bool obdDecodeThrottle(const uint8_t *data, uint8_t dlc, OBDReading &out)
{
  const uint8_t *p;
  if (!checkHeader(data, dlc, 0x11, 3, p))
    return false;
  out.throttlePct = (uint8_t)((uint16_t)p[0] * 100 / 255);
  return true;
}

bool obdDecodeTiming(const uint8_t *data, uint8_t dlc, OBDReading &out)
{
  const uint8_t *p;
  if (!checkHeader(data, dlc, 0x0E, 3, p))
    return false;
  out.timingDeg = p[0] / 2.0f - 64.0f;
  return true;
}

bool obdDecodeEcuVoltage(const uint8_t *data, uint8_t dlc, OBDReading &out)
{
  const uint8_t *p;
  if (!checkHeader(data, dlc, 0x42, 4, p))
    return false;
  out.ecuVoltage = ((uint16_t)p[0] * 256 + p[1]) / 1000.0f;
  return true;
}

bool obdDecodeMafAlt(const uint8_t *data, uint8_t dlc, OBDReading &out)
{
  const uint8_t *p;
  if (!checkHeader(data, dlc, 0x66, 5, p))
    return false;
  if (!(p[0] & 0x01)) // Sensor1 MAF 非搭載
    return false;
  out.mafGs = ((uint16_t)p[1] * 256 + p[2]) / 32.0f;
  return true;
}

bool obdDecodeCoolantAlt(const uint8_t *data, uint8_t dlc, OBDReading &out)
{
  const uint8_t *p;
  if (!checkHeader(data, dlc, 0x67, 4, p))
    return false;
  if (!(p[0] & 0x01)) // Sensor1 非搭載
    return false;
  out.coolantC = (int16_t)p[1] - 40;
  return true;
}

bool obdDecodeShortTermFuelTrim(const uint8_t *data, uint8_t dlc, OBDReading &out)
{
  const uint8_t *p;
  if (!checkHeader(data, dlc, 0x06, 3, p))
    return false;
  out.stftPct = ((int)p[0] - 128) * 100.0f / 128.0f;
  return true;
}

bool obdDecodeLongTermFuelTrim(const uint8_t *data, uint8_t dlc, OBDReading &out)
{
  const uint8_t *p;
  if (!checkHeader(data, dlc, 0x07, 3, p))
    return false;
  out.ltftPct = ((int)p[0] - 128) * 100.0f / 128.0f;
  return true;
}

bool obdDecodeO2SensorB1S2(const uint8_t *data, uint8_t dlc, OBDReading &out)
{
  const uint8_t *p;
  if (!checkHeader(data, dlc, 0x15, 4, p))
    return false;
  out.o2B1s2V = p[0] / 200.0f;
  out.o2B1s2TrimPct = ((int)p[1] - 128) * 100.0f / 128.0f;
  return true;
}

bool obdDecodeEngineRunTime(const uint8_t *data, uint8_t dlc, OBDReading &out)
{
  const uint8_t *p;
  if (!checkHeader(data, dlc, 0x1F, 4, p))
    return false;
  out.engineRunTimeSec = (uint16_t)p[0] * 256 + p[1];
  return true;
}

bool obdDecodeMilDistance(const uint8_t *data, uint8_t dlc, OBDReading &out)
{
  const uint8_t *p;
  if (!checkHeader(data, dlc, 0x21, 4, p))
    return false;
  out.milDistanceKm = (uint16_t)p[0] * 256 + p[1];
  return true;
}

bool obdDecodeO2Sensor1WideBand(const uint8_t *data, uint8_t dlc, OBDReading &out)
{
  const uint8_t *p;
  if (!checkHeader(data, dlc, 0x24, 6, p))
    return false;
  out.o2S1Ratio = ((uint16_t)p[0] * 256 + p[1]) * 2.0f / 65536.0f;
  out.o2S1Voltage = ((uint16_t)p[2] * 256 + p[3]) * 8.0f / 65536.0f;
  return true;
}

bool obdDecodeEvapPurge(const uint8_t *data, uint8_t dlc, OBDReading &out)
{
  const uint8_t *p;
  if (!checkHeader(data, dlc, 0x2E, 3, p))
    return false;
  out.evapPurgePct = (uint8_t)((uint16_t)p[0] * 100 / 255);
  return true;
}

bool obdDecodeWarmupsSinceCleared(const uint8_t *data, uint8_t dlc, OBDReading &out)
{
  const uint8_t *p;
  if (!checkHeader(data, dlc, 0x30, 3, p))
    return false;
  out.warmupsSinceCleared = p[0];
  return true;
}

bool obdDecodeDistanceSinceCleared(const uint8_t *data, uint8_t dlc, OBDReading &out)
{
  const uint8_t *p;
  if (!checkHeader(data, dlc, 0x31, 4, p))
    return false;
  out.distanceSinceClearedKm = (uint16_t)p[0] * 256 + p[1];
  return true;
}

bool obdDecodeCatalystTemp(const uint8_t *data, uint8_t dlc, OBDReading &out)
{
  const uint8_t *p;
  if (!checkHeader(data, dlc, 0x3C, 4, p))
    return false;
  out.catalystTempC = ((uint16_t)p[0] * 256 + p[1]) / 10.0f - 40.0f;
  return true;
}

bool obdDecodeAbsoluteLoad(const uint8_t *data, uint8_t dlc, OBDReading &out)
{
  const uint8_t *p;
  if (!checkHeader(data, dlc, 0x43, 4, p))
    return false;
  out.absoluteLoadPct = ((uint16_t)p[0] * 256 + p[1]) * 100.0f / 255.0f;
  return true;
}

bool obdDecodeCommandedAfr(const uint8_t *data, uint8_t dlc, OBDReading &out)
{
  const uint8_t *p;
  if (!checkHeader(data, dlc, 0x44, 4, p))
    return false;
  out.commandedAfr = ((uint16_t)p[0] * 256 + p[1]) * 2.0f / 65536.0f;
  return true;
}

bool obdDecodeThrottleB(const uint8_t *data, uint8_t dlc, OBDReading &out)
{
  const uint8_t *p;
  if (!checkHeader(data, dlc, 0x47, 3, p))
    return false;
  out.throttleBPct = (uint8_t)((uint16_t)p[0] * 100 / 255);
  return true;
}

bool obdDecodeAccelPedalD(const uint8_t *data, uint8_t dlc, OBDReading &out)
{
  const uint8_t *p;
  if (!checkHeader(data, dlc, 0x49, 3, p))
    return false;
  out.accelPedalDPct = (uint8_t)((uint16_t)p[0] * 100 / 255);
  return true;
}

bool obdDecodeAccelPedalE(const uint8_t *data, uint8_t dlc, OBDReading &out)
{
  const uint8_t *p;
  if (!checkHeader(data, dlc, 0x4A, 3, p))
    return false;
  out.accelPedalEPct = (uint8_t)((uint16_t)p[0] * 100 / 255);
  return true;
}

bool obdDecodeFuelType(const uint8_t *data, uint8_t dlc, OBDReading &out)
{
  const uint8_t *p;
  if (!checkHeader(data, dlc, 0x51, 3, p))
    return false;
  out.fuelType = p[0];
  return true;
}

bool obdDecodeSecO2TrimShortTerm(const uint8_t *data, uint8_t dlc, OBDReading &out)
{
  const uint8_t *p;
  if (!checkHeader(data, dlc, 0x55, 3, p))
    return false;
  out.secO2TrimStPct = ((int)p[0] - 128) * 100.0f / 128.0f;
  return true;
}

bool obdDecodeSecO2TrimLongTerm(const uint8_t *data, uint8_t dlc, OBDReading &out)
{
  const uint8_t *p;
  if (!checkHeader(data, dlc, 0x56, 3, p))
    return false;
  out.secO2TrimLtPct = ((int)p[0] - 128) * 100.0f / 128.0f;
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
    out.iatC = (int16_t)p[1] - 40;
    any = true;
  }
  if (p[0] & 0x02) // Sensor2 搭載
  {
    out.iat2C = (int16_t)p[2] - 40;
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
  out.speedKmh = r.speedKmh;
  out.loadPct = r.loadPct;
  out.mapKpa = r.mapKpa;
  out.baroKpa = r.baroKpa;
  out.boostKpa = r.boostKpa;
  out.throttlePct = r.throttlePct;
  out.timingDeg = r.timingDeg;
  out.ecuVoltage = r.ecuVoltage;
  out.mafGs = r.mafGs;
  out.coolantC = r.coolantC;
  out.fuelRateLph = r.fuelRateLph;

  out.stftPct = r.stftPct;
  out.ltftPct = r.ltftPct;
  out.o2B1s2V = r.o2B1s2V;
  out.o2B1s2TrimPct = r.o2B1s2TrimPct;
  out.engineRunTimeSec = r.engineRunTimeSec;
  out.milDistanceKm = r.milDistanceKm;
  out.o2S1Ratio = r.o2S1Ratio;
  out.o2S1Voltage = r.o2S1Voltage;
  out.evapPurgePct = r.evapPurgePct;
  out.warmupsSinceCleared = r.warmupsSinceCleared;
  out.distanceSinceClearedKm = r.distanceSinceClearedKm;
  out.catalystTempC = r.catalystTempC;
  out.absoluteLoadPct = r.absoluteLoadPct;
  out.commandedAfr = r.commandedAfr;
  out.throttleBPct = r.throttleBPct;
  out.accelPedalDPct = r.accelPedalDPct;
  out.accelPedalEPct = r.accelPedalEPct;
  out.fuelType = r.fuelType;
  out.secO2TrimStPct = r.secO2TrimStPct;
  out.secO2TrimLtPct = r.secO2TrimLtPct;

  out.valid = r.valid ? 1 : 0;
  out.ts = (uint32_t)r.ts;

  out.iatC = r.iatC;
  out.iat2C = r.iat2C;

  out.validMask = r.validMask;
}

void obdComputeDerived(OBDReading &r)
{
  if (!r.valid)
    return;

  // boost = MAP - Baro（Baro未取得時は標準大気圧101kPaで代用）
  r.boostKpa = (int8_t)(r.mapKpa - (r.baroKpa > 0 ? r.baroKpa : 101));
  // 燃費推算（MAFから）: OBD.md 参照
  if (r.mafGs > 0)
    r.fuelRateLph = r.mafGs / (14.7f * 0.745f) * 3.6f;
}
