#include "obdpoll.h"
#include "diddscan.h"
#include "../logger.h"
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
// OBDReading::validMask（uint32_t）はkPidsの配列順にビット割当するため32PIDが上限
static_assert(sizeof(kPids) / sizeof(kPids[0]) <= 32, "kPidsが32件を超えるとvalidMask(uint32_t)に収まらない");

// 1リクエストにまとめるPID数の上限。SFリクエストのPCI長（1+N）が7バイトに収まる上限（N<=6）。
const uint8_t kMaxPidsPerRequest = 6;

// kPidLengths（obd.cpp）上の最大値（0x68 の7バイトが最大）。
// セグメントを`41 [pid] data...`形式に詰め直す一時バッファのサイズに使う。
const uint8_t kMaxSegmentDataLen = 7;

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
    ctx->r->validMask |= (1u << (uint32_t)(dec - kPids));
    ctx->okCount++;
  }
  else
  {
    ctx->decodeFailCount++;
  }
}

// 派生値計算（domain/obd.cppに委譲）とログ出力
void finalizeAndLog(OBDReading &r)
{
  obdComputeDerived(r);

  if (r.valid)
  {
    logger.printf("[OBD] rpm=%u speed=%ukm/h load=%u%% map=%ukPa boost=%dkPa throttle=%u%% "
                  "timing=%.1f ecu=%.2fV maf=%.2fg/s coolant=%dC fuel=%.2fL/h mask=0x%08X\n",
                  r.rpm, r.speedKmh, r.loadPct, r.mapKpa, r.boostKpa, r.throttlePct,
                  r.timingDeg, r.ecuVoltage, r.mafGs, r.coolantC, r.fuelRateLph,
                  (unsigned)r.validMask);

    // 追加確定PID（1行が長大になるため既存サマリ行とは分けて出力）
    logger.printf("[OBD2] stft=%.1f%% ltft=%.1f%% o2b1s2=%.2fV/%.1f%% o2s1=%.3f/%.2fV "
                  "runtime=%us milDist=%ukm evap=%u%% warmups=%u distCleared=%ukm "
                  "cat=%.0fC absLoad=%.1f%% afr=%.2f tpsB=%u%% padD=%u%% padE=%u%% "
                  "fuelType=%u secO2st=%.1f%% secO2lt=%.1f%% iat=%dC/%dC atf=%dC(valid=%d)\n",
                  r.stftPct, r.ltftPct, r.o2B1s2V, r.o2B1s2TrimPct,
                  r.o2S1Ratio, r.o2S1Voltage,
                  r.engineRunTimeSec, r.milDistanceKm, r.evapPurgePct,
                  r.warmupsSinceCleared, r.distanceSinceClearedKm,
                  r.catalystTempC, r.absoluteLoadPct, r.commandedAfr,
                  r.throttleBPct, r.accelPedalDPct, r.accelPedalEPct,
                  r.fuelType, r.secO2TrimStPct, r.secO2TrimLtPct,
                  r.iatC, r.iat2C, r.atfTempC, (int)r.atfTempValid);
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
  unsigned long pollStartMs = millis();

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
    if (canReceiveObdResponse(data, &dlc, 50, sizeof(data)) != ObdRecvResult::Ok)
    {
      recvFailCount += groupCount;
      continue;
    }

    SegmentCtx ctx = {};
    ctx.r = &r;
    ctx.groupPids = groupPids;
    ctx.groupCount = groupCount;
    uint8_t unknownPid = 0;
    if (obdParseMultiResponse(data, dlc, handleSegment, &ctx, &unknownPid))
      logger.printf("[OBD] obdParseMultiResponse: 未知PID 0x%02X で打ち切り\n", unknownPid);

    okCount += ctx.okCount;
    decodeFailCount += ctx.decodeFailCount;
    for (uint8_t i = 0; i < groupCount; i++)
      if (!ctx.seen[i])
        missingCount++;
  }

  // Mode22 (UDS) 追加分: ATF油温（DID 0x2201）。Mode01の多PIDバッチ機構とは別経路の単発問い合わせ。
  // 実車未テスト（OBD.md「Mode 22 実機テスト候補」参照）。
  if (!canSendObdRequestUds(0x2201))
  {
    logger.println("[OBD] ATF(0x2201): 送信失敗");
  }
  else if (canReceiveObdResponse(data, &dlc, 50, sizeof(data)) != ObdRecvResult::Ok)
  {
    logger.println("[OBD] ATF(0x2201): 応答なし");
  }
  else if (!obdDecodeAtfTemp(data, dlc, r))
  {
    logger.println("[OBD] ATF(0x2201): デコード失敗");
  }
  else
  {
    r.atfTempValid = true;
  }

  // 燃料残量・油温DID調査用（OBD.md「DID Values 実測記録」参照）。走行中もCONTINUOUSモードの
  // 1秒間隔ポーリングに相乗りさせることで、PA/SA停車時のスナップショットだけでなく走行中の
  // 連続データを取れるようにする。結果はresult自体は使わずログ出力のみ（BLE/AWSには送らない）。
  DidValueResult didValueResult;
  didReadCandidateValues(didValueResult);

  logger.printf("[OBD] poll: OK=%d/%d 送信失敗=%d 応答なし=%d デコード失敗=%d 未応答PID=%d 所要%lums\n",
                okCount, kPidCount, sendFailCount, recvFailCount, decodeFailCount, missingCount,
                millis() - pollStartMs);
  finalizeAndLog(r);
  return r;
}

OBDReading obdPollFake()
{
  // CAN未接続時のobdPoll()は5グループ×50msタイムアウト+UDS1回×50ms≒300ms程度かかる。
  // obdPollFake()が即座に返るとonTick()内の処理時間特性が本番と乖離するため、
  // 同程度delayして他処理（BLE Notify送信・OLED更新等）とのタイミング条件を近づける。
  delay(300);

  OBDReading r = {};
  r.ts = time(nullptr);
  r.valid = true;
  r.validMask = 0xFFFFFFFFu;

  // アイドリング相当の固定値。obdComputeDerived()が算出するboostKpa/fuelRateLphは
  // mapKpa/baroKpa/mafGsから自動計算されるためここでは設定しない。
  // rpmだけランダムに揺らす（car_iot_android側のミニグラフ・スパークライン表示の
  // 動作確認用、値が完全固定だと折れ線に変化が出ないため）。
  r.rpm = 800 + random(-50, 51);
  r.speedKmh = 0;
  r.loadPct = 20;
  r.mapKpa = 35;
  r.baroKpa = 101;
  r.throttlePct = 15;
  r.timingDeg = 12.5f;
  r.ecuVoltage = 14.2f;
  r.mafGs = 3.5f;
  r.coolantC = 85;

  r.stftPct = 2.5f;
  r.ltftPct = -1.0f;
  r.o2B1s2V = 0.65f;
  r.o2B1s2TrimPct = 0.0f;
  r.engineRunTimeSec = 120;
  r.milDistanceKm = 0;
  r.o2S1Ratio = 1.0f;
  r.o2S1Voltage = 2.5f;
  r.evapPurgePct = 0;
  r.warmupsSinceCleared = 5;
  r.distanceSinceClearedKm = 250;
  r.catalystTempC = 450.0f;
  r.absoluteLoadPct = 25.0f;
  r.commandedAfr = 1.0f;
  r.throttleBPct = 15;
  r.accelPedalDPct = 10;
  r.accelPedalEPct = 10;
  r.fuelType = 1; // ガソリン
  r.secO2TrimStPct = 0.0f;
  r.secO2TrimLtPct = 0.0f;

  r.iatC = 25;
  r.iat2C = 26;

  r.atfTempC = 60;
  r.atfTempValid = true;

  obdComputeDerived(r); // boostKpa/fuelRateLphを算出
  logger.printf("[OBD] obdPollFake(): ダミーデータ送出中（DEBUG_FAKE_OBD_DATA有効）\n");
  return r;
}

bool obdCheckCanAlive()
{
  uint8_t pid = 0x0C;
  uint8_t data[8];
  uint8_t dlc;
  if (!canSendObdRequestMulti(&pid, 1))
    return false;
  return canReceiveObdResponse(data, &dlc, 50, sizeof(data)) == ObdRecvResult::Ok;
}
