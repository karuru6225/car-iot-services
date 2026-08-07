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
    {0x68, obdDecodeChargeAirTemp},
};
const int kPidCount = sizeof(kPids) / sizeof(kPids[0]);

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

// 診断用（HANDOFF_isotp_multipid.md §4 テスト2、タスク2/3の実車確認用。結論が出たら削除すること）
void logMultiPidSegment(uint8_t pid, const uint8_t *segData, uint8_t len, void *)
{
  logger.printf("[TEST2] pid=0x%02X len=%u data=", pid, len);
  for (uint8_t i = 0; i < len; i++)
    logger.printf(" %02X", segData[i]);
  logger.println();
}
} // namespace

OBDReading obdPoll()
{
  OBDReading r = {};
  r.ts = time(nullptr);

  // 64バイト: 0x68等マルチフレーム応答を受けられるだけの余裕を持たせる
  // （HANDOFF_isotp_multipid.md タスク1「当面64バイトあれば十分」）
  uint8_t data[64];
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
    if (!canReceiveObdResponse(data, &dlc, 50, sizeof(data)))
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

  // 診断用（HANDOFF_isotp_multipid.md §4 テスト2、原因B確定後のタスク2/3実車確認用。
  // 通常の29PIDポーリングとは独立、r/okCount等の集計には含めない。結論が出たら削除すること）
  {
    static const uint8_t kTestPids[] = {0x0C, 0x0B}; // RPM, MAP（§4テスト1と同じ組）
    if (canSendObdRequestMulti(kTestPids, sizeof(kTestPids)))
    {
      uint8_t testData[16];
      uint8_t testDlc;
      if (canReceiveObdResponse(testData, &testDlc, 50, sizeof(testData)))
        obdParseMultiResponse(testData, testDlc, logMultiPidSegment, nullptr);
      else
        logger.println("[TEST2] 応答なし");
    }
    else
    {
      logger.println("[TEST2] 送信失敗");
    }
  }

  return r;
}
