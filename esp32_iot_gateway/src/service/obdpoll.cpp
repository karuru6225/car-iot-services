#include "obdpoll.h"
#include "logger.h"
#include "../device/can.h"
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
};
const int kPidCount = sizeof(kPids) / sizeof(kPids[0]);
const int kBulkGroupSize = 6; // ISO 15765-4 Single Frame: 8 - PCI(1) - Mode(1) = 6バイト

// boost/燃費の派生値計算とログ出力。obdPoll()/obdPollBulk()共通
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
                  "fuelType=%u secO2st=%.1f%% secO2lt=%.1f%%\n",
                  r.stft_pct, r.ltft_pct, r.o2_b1s2_v, r.o2_b1s2_trim_pct,
                  r.o2_s1_ratio, r.o2_s1_voltage,
                  r.engine_run_time_sec, r.mil_distance_km, r.evap_purge_pct,
                  r.warmups_since_cleared, r.distance_since_cleared_km,
                  r.catalyst_temp_c, r.absolute_load_pct, r.commanded_afr,
                  r.throttle_b_pct, r.accel_pedal_d_pct, r.accel_pedal_e_pct,
                  r.fuel_type, r.sec_o2_trim_st_pct, r.sec_o2_trim_lt_pct);
  }
  else
  {
    logger.println("[OBD] 応答なし（IGN OFF または CAN 未接続）");
  }
}

int s_bulkTestCyclesRemaining = 0;
} // namespace

OBDReading obdPoll()
{
  OBDReading r = {};
  r.ts = time(nullptr);

  uint8_t data[8];
  uint8_t dlc;
  int okCount = 0, sendFailCount = 0, recvFailCount = 0, decodeFailCount = 0;

  for (const auto &p : kPids)
  {
    // 28PIDに増えたため異常時（IGN OFF等）の最悪サイクル時間を抑える目的でタイムアウトを短縮
    // （実車では正常応答は数十msで返る実績があるため、50msでも正常系には影響しない）
    if (!canSendObdRequest(p.pid))
    {
      sendFailCount++;
      continue;
    }
    if (!canReceiveObdResponse(data, &dlc, 50))
    {
      recvFailCount++;
      continue;
    }
    if (p.decode(data, dlc, r))
    {
      r.valid = true;
      okCount++;
    }
    else
    {
      decodeFailCount++;
    }
  }

  logger.printf("[OBD] poll: OK=%d/%d 送信失敗=%d 応答なし=%d デコード失敗=%d\n",
                okCount, kPidCount, sendFailCount, recvFailCount, decodeFailCount);
  finalizeAndLog(r);
  return r;
}

OBDReading obdPollBulk()
{
  OBDReading r = {};
  r.ts = time(nullptr);

  uint8_t data[8];
  uint8_t dlc;
  int groupIndex = 0, sendFailGroups = 0, respTotal = 0;

  for (int base = 0; base < kPidCount; base += kBulkGroupSize)
  {
    int groupCount = (kPidCount - base < kBulkGroupSize) ? (kPidCount - base) : kBulkGroupSize;
    groupIndex++;

    uint8_t pids[kBulkGroupSize];
    for (int i = 0; i < groupCount; i++)
      pids[i] = kPids[base + i].pid;

    if (!canSendObdRequestBulk(pids, (uint8_t)groupCount))
    {
      sendFailGroups++;
      logger.printf("[OBD] バルクグループ%d: 送信失敗（PID%d個）\n", groupIndex, groupCount);
      continue; // 送信失敗時はこのグループを諦めて次へ
    }

    // グループ内PID数だけ応答を試みる。ECUが複数フレームで個別に返す想定（未検証）
    int respCount = 0;
    for (int i = 0; i < groupCount; i++)
    {
      if (!canReceiveObdResponse(data, &dlc, 50))
        break; // タイムアウトしたらこのグループは打ち切り、次のグループへ
      respCount++;

      uint8_t respPid = data[2];
      for (int j = 0; j < groupCount; j++)
      {
        if (kPids[base + j].pid == respPid)
        {
          if (kPids[base + j].decode(data, dlc, r))
            r.valid = true;
          break;
        }
      }
    }
    respTotal += respCount;
    logger.printf("[OBD] バルクグループ%d: PID%d個送信 → 応答%d個\n", groupIndex, groupCount, respCount);
  }

  logger.printf("[OBD] バルクポーリング完了: %dグループ中送信失敗%d、応答合計%d/%d\n",
                groupIndex, sendFailGroups, respTotal, kPidCount);
  finalizeAndLog(r);
  return r;
}

void requestObdBulkTest(int cycles)
{
  s_bulkTestCyclesRemaining = cycles;
  logger.printf("[OBD] バルクテスト開始（%d サイクル）\n", cycles);
}

OBDReading obdPollTick()
{
  if (s_bulkTestCyclesRemaining > 0)
  {
    s_bulkTestCyclesRemaining--;
    return obdPollBulk();
  }
  return obdPoll();
}
