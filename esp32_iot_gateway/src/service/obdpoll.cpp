#include "obdpoll.h"
#include "logger.h"
#include "../device/can.h"
#include <string.h>
#include <time.h>

namespace
{
struct PidDecoder
{
  uint8_t pid;
  bool (*decode)(const uint8_t *, uint8_t, OBDReading &);
};

const PidDecoder kPids[] = {
    {0x0C, obdDecodeRpm},
    {0x0D, obdDecodeSpeed},
    {0x04, obdDecodeLoad},
    {0x0B, obdDecodeMap},
    {0x33, obdDecodeBaro},
    {0x11, obdDecodeThrottle},
    {0x0E, obdDecodeTiming},
    {0x42, obdDecodeEcuVoltage},
    {0x66, obdDecodeMafAlt},
    {0x67, obdDecodeCoolantAlt},
    // 追加確定PID（OBD.md「確定した取得可能データ一覧」参照）
    {0x06, obdDecodeShortTermFuelTrim},
    {0x07, obdDecodeLongTermFuelTrim},
    {0x15, obdDecodeO2SensorB1S2},
    {0x1F, obdDecodeEngineRunTime},
    {0x21, obdDecodeMilDistance},
    {0x24, obdDecodeO2Sensor1WideBand},
    {0x2E, obdDecodeEvapPurge},
    {0x30, obdDecodeWarmupsSinceCleared},
    {0x31, obdDecodeDistanceSinceCleared},
    {0x3C, obdDecodeCatalystTemp},
    {0x43, obdDecodeAbsoluteLoad},
    {0x44, obdDecodeCommandedAfr},
    {0x47, obdDecodeThrottleB},
    {0x49, obdDecodeAccelPedalD},
    {0x4A, obdDecodeAccelPedalE},
    {0x51, obdDecodeFuelType},
    {0x55, obdDecodeSecO2TrimShortTerm},
    {0x56, obdDecodeSecO2TrimLongTerm},
    {0x68, obdDecodeChargeAirTemp},
};
const int kPidCount = sizeof(kPids) / sizeof(kPids[0]);

// 1リクエストにまとめるPID数の上限。SFリクエストのPCI長（1+N）が7バイトに収まる上限（N<=6）。
const uint8_t kMaxPidsPerRequest = 6;

// obd.h内の各obdDecode*()が消費するデータ長の最大値（0x24 ワイドバンドO2の4バイトが最大）。
// セグメントを`41 [pid] data...`形式に詰め直す一時バッファのサイズに使う。
const uint8_t kMaxSegmentDataLen = 4;

const PidDecoder *findDecoder(uint8_t pid)
{
  for (const auto &p : kPids)
    if (p.pid == pid)
      return &p;
  return nullptr;
}

// obdParseMultiResponse()のコールバック用コンテキスト（1リクエストグループ分）
struct SegmentCtx
{
  OBDReading *r;
  const uint8_t *groupPids;
  uint8_t groupCount;
  bool seen[kMaxPidsPerRequest];
  int okCount;
  int decodeFailCount;
};

// 多PID応答の1セグメントを既存のobdDecode*()に橋渡しする。既存デコーダは
// checkHeader()経由で`41 [pid] [data...]`形式を前提にしているため、セグメントを
// 一時バッファに詰め直して呼び出す（デコーダ本体は変更しない）。
void handleSegment(uint8_t pid, const uint8_t *segData, uint8_t len, void *ctxPtr)
{
  auto *ctx = static_cast<SegmentCtx *>(ctxPtr);

  for (uint8_t i = 0; i < ctx->groupCount; i++)
    if (ctx->groupPids[i] == pid)
      ctx->seen[i] = true;

  const PidDecoder *dec = findDecoder(pid);
  if (!dec || len > kMaxSegmentDataLen)
    return; // グループに含めていないPID、または想定外に長いデータは無視

  uint8_t tmp[2 + kMaxSegmentDataLen];
  tmp[0] = 0x41;
  tmp[1] = pid;
  memcpy(tmp + 2, segData, len);

  if (dec->decode(tmp, 2 + len, *ctx->r))
  {
    ctx->r->valid = true;
    ctx->okCount++;
  }
  else
  {
    ctx->decodeFailCount++;
  }
}

// boost/燃費の派生値計算とログ出力
void finalizeAndLog(OBDReading &r)
{
  if (r.valid)
  {
    // boost = MAP - Baro（Baro未取得時は標準大気圧101kPaで代用）
    r.boost_kpa = (int8_t)(r.map_kpa - (r.baro_kpa > 0 ? r.baro_kpa : 101));
    // 燃費推算（MAFから）: OBD.md 参照
    if (r.maf_gs > 0)
      r.fuel_rate_lph = r.maf_gs / (14.7f * 0.745f) * 3.6f;

    logger.printf("[OBD] rpm=%u speed=%ukm/h load=%u%% map=%ukPa boost=%dkPa throttle=%u%% "
                  "timing=%.1f ecu=%.2fV maf=%.2fg/s coolant=%dC fuel=%.2fL/h\n",
                  r.rpm, r.speed_kmh, r.load_pct, r.map_kpa, r.boost_kpa, r.throttle_pct,
                  r.timing_deg, r.ecu_voltage, r.maf_gs, r.coolant_c, r.fuel_rate_lph);

    // 追加確定PID（1行が長大になるため既存サマリ行とは分けて出力）
    logger.printf("[OBD2] stft=%.1f%% ltft=%.1f%% o2b1s2=%.2fV/%.1f%% o2s1=%.3f/%.2fV "
                  "runtime=%us milDist=%ukm evap=%u%% warmups=%u distCleared=%ukm "
                  "cat=%.0fC absLoad=%.1f%% afr=%.2f tpsB=%u%% padD=%u%% padE=%u%% "
                  "fuelType=%u secO2st=%.1f%% secO2lt=%.1f%% iat=%dC/%dC\n",
                  r.stft_pct, r.ltft_pct, r.o2_b1s2_v, r.o2_b1s2_trim_pct,
                  r.o2_s1_ratio, r.o2_s1_voltage,
                  r.engine_run_time_sec, r.mil_distance_km, r.evap_purge_pct,
                  r.warmups_since_cleared, r.distance_since_cleared_km,
                  r.catalyst_temp_c, r.absolute_load_pct, r.commanded_afr,
                  r.throttle_b_pct, r.accel_pedal_d_pct, r.accel_pedal_e_pct,
                  r.fuel_type, r.sec_o2_trim_st_pct, r.sec_o2_trim_lt_pct,
                  r.iat_c, r.iat2_c);
  }
  else
  {
    logger.println("[OBD] 応答なし（IGN OFF または CAN 未接続）");
    canLogStatus("全PID応答なし");
  }
}
} // namespace

OBDReading obdPoll()
{
  OBDReading r = {};
  r.ts = time(nullptr);

  // 64バイト: 6PIDまとめ要求時の最大応答（ワイドバンドO2等を含む組み合わせ）や
  // 0x68等のマルチフレーム応答を受けられるだけの余裕を持たせる
  uint8_t data[64];
  uint8_t dlc;
  int okCount = 0, sendFailCount = 0, recvFailCount = 0, decodeFailCount = 0, missingCount = 0;

  for (int start = 0; start < kPidCount; start += kMaxPidsPerRequest)
  {
    uint8_t groupCount = (uint8_t)((kPidCount - start < kMaxPidsPerRequest)
                                        ? (kPidCount - start)
                                        : kMaxPidsPerRequest);
    uint8_t groupPids[kMaxPidsPerRequest];
    for (uint8_t i = 0; i < groupCount; i++)
      groupPids[i] = kPids[start + i].pid;

    // 29PID→最大6PIDずつ計5リクエストに集約したため、異常時（IGN OFF等）の
    // 最悪サイクル時間は5×50ms=250ms程度に収まる（旧実装の29×50msから大幅短縮）
    if (!canSendObdRequestMulti(groupPids, groupCount))
    {
      sendFailCount += groupCount;
      continue;
    }
    if (!canReceiveObdResponse(data, &dlc, 50, sizeof(data)))
    {
      recvFailCount += groupCount;
      continue;
    }

    SegmentCtx ctx = {};
    ctx.r = &r;
    ctx.groupPids = groupPids;
    ctx.groupCount = groupCount;
    obdParseMultiResponse(data, dlc, handleSegment, &ctx);

    okCount += ctx.okCount;
    decodeFailCount += ctx.decodeFailCount;
    for (uint8_t i = 0; i < groupCount; i++)
      if (!ctx.seen[i])
        missingCount++;
  }

  logger.printf("[OBD] poll: OK=%d/%d 送信失敗=%d 応答なし=%d デコード失敗=%d 未応答PID=%d\n",
                okCount, kPidCount, sendFailCount, recvFailCount, decodeFailCount, missingCount);
  finalizeAndLog(r);
  return r;
}
