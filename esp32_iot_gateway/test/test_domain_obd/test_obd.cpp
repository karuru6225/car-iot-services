#include <unity.h>
#include "domain/obd.h"

void setUp(void) {}
void tearDown(void) {}

// ─── 基本デコード ────────────────────────────────────────────────────────────

static void test_rpm_decodes_valid_response(void)
{
  uint8_t data[] = {0x41, 0x0C, 0x1A, 0xF8};
  OBDReading out = {};
  TEST_ASSERT_TRUE(obdDecodeRpm(data, sizeof(data), out));
  TEST_ASSERT_EQUAL_UINT16(1726, out.rpm); // (0x1A*256+0xF8)/4
}

static void test_rpm_rejects_wrong_pid(void)
{
  uint8_t data[] = {0x41, 0x0D, 0x1A, 0xF8}; // 応答PIDが0x0Dで要求(0x0C)と不一致
  OBDReading out = {};
  TEST_ASSERT_FALSE(obdDecodeRpm(data, sizeof(data), out));
}

static void test_rpm_rejects_short_dlc(void)
{
  uint8_t data[] = {0x41, 0x0C, 0x1A}; // dlc=3 < minDlc(4)
  OBDReading out = {};
  TEST_ASSERT_FALSE(obdDecodeRpm(data, sizeof(data), out));
}

static void test_speed_decodes_valid_response(void)
{
  uint8_t data[] = {0x41, 0x0D, 0x50};
  OBDReading out = {};
  TEST_ASSERT_TRUE(obdDecodeSpeed(data, sizeof(data), out));
  TEST_ASSERT_EQUAL_UINT8(0x50, out.speedKmh);
}

// ─── マルチセンサーPID（bitmap検証）──────────────────────────────────────────
// obd.h の PID 0x67 コメント: bitmap=data[2]（0x03=S1+S2）
// bitmapでSensor1が非搭載と申告されている場合、そのバイトにセンサー値は
// 入っていないはずなので、デコーダはfalseを返し out を書き換えるべき。

static void test_coolant_alt_decodes_when_sensor1_present(void)
{
  uint8_t data[] = {0x41, 0x67, 0x03, 90}; // bitmap=S1+S2, Sensor1=90(->50C)
  OBDReading out = {};
  TEST_ASSERT_TRUE(obdDecodeCoolantAlt(data, sizeof(data), out));
  TEST_ASSERT_EQUAL_INT16(50, out.coolantC);
}

static void test_coolant_alt_rejects_when_sensor1_absent(void)
{
  uint8_t data[] = {0x41, 0x67, 0x02, 90}; // bitmap=S2のみ、Sensor1非搭載
  OBDReading out = {};
  TEST_ASSERT_FALSE(obdDecodeCoolantAlt(data, sizeof(data), out));
}

static void test_charge_air_temp_decodes_both_sensors(void)
{
  uint8_t data[] = {0x41, 0x68, 0x03, 90, 70, 0, 0}; // bitmap=S1+S2
  OBDReading out = {};
  TEST_ASSERT_TRUE(obdDecodeChargeAirTemp(data, sizeof(data), out));
  TEST_ASSERT_EQUAL_INT16(50, out.iatC);
  TEST_ASSERT_EQUAL_INT16(30, out.iat2C);
}

static void test_charge_air_temp_decodes_sensor2_only(void)
{
  uint8_t data[] = {0x41, 0x68, 0x02, 90, 70, 0, 0}; // bitmap=S2のみ
  OBDReading out = {};
  out.iatC = 999; // Sensor1が更新されないことを確認するための番兵値
  TEST_ASSERT_TRUE(obdDecodeChargeAirTemp(data, sizeof(data), out));
  TEST_ASSERT_EQUAL_INT16(999, out.iatC); // 非搭載センサーは書き換えない
  TEST_ASSERT_EQUAL_INT16(30, out.iat2C);
}

static void test_maf_alt_decodes_when_sensor1_present(void)
{
  uint8_t data[] = {0x41, 0x66, 0x01, 0x01, 0x00}; // sensors=S1のみ、MAF=(1*256+0)/32
  OBDReading out = {};
  TEST_ASSERT_TRUE(obdDecodeMafAlt(data, sizeof(data), out));
  TEST_ASSERT_EQUAL_FLOAT(8.0f, out.mafGs);
}

static void test_maf_alt_rejects_when_sensor1_absent(void)
{
  uint8_t data[] = {0x41, 0x66, 0x00, 0x01, 0x00}; // sensors=0、Sensor1非搭載
  OBDReading out = {};
  TEST_ASSERT_FALSE(obdDecodeMafAlt(data, sizeof(data), out));
}

// ─── 多PIDバッチ応答の分解 ────────────────────────────────────────────────────

static uint8_t g_capturedCount;
static uint8_t g_capturedPids[8];
static uint8_t g_capturedLens[8];

static void multiCb(uint8_t pid, const uint8_t *data, uint8_t len, void *ctx)
{
  (void)data;
  (void)ctx;
  if (g_capturedCount < 8)
  {
    g_capturedPids[g_capturedCount] = pid;
    g_capturedLens[g_capturedCount] = len;
    g_capturedCount++;
  }
}

static void test_multi_response_parses_two_known_pids(void)
{
  // 41 [0C][A][B] [0D][A] : rpm(0x0C,2byte) → speed(0x0D,1byte)
  uint8_t data[] = {0x41, 0x0C, 0x1A, 0xF8, 0x0D, 0x50};
  g_capturedCount = 0;
  obdParseMultiResponse(data, sizeof(data), multiCb, nullptr);
  TEST_ASSERT_EQUAL_UINT8(2, g_capturedCount);
  TEST_ASSERT_EQUAL_UINT8(0x0C, g_capturedPids[0]);
  TEST_ASSERT_EQUAL_UINT8(2, g_capturedLens[0]);
  TEST_ASSERT_EQUAL_UINT8(0x0D, g_capturedPids[1]);
  TEST_ASSERT_EQUAL_UINT8(1, g_capturedLens[1]);
}

static void test_multi_response_stops_at_unknown_pid(void)
{
  // 0xFF はkPidLengthsに無い未知PID → 以降のセグメント境界が分からないため打ち切り
  uint8_t data[] = {0x41, 0x0C, 0x1A, 0xF8, 0xFF, 0x99, 0x0D, 0x50};
  g_capturedCount = 0;
  uint8_t unknownPid = 0;
  bool hitUnknown = obdParseMultiResponse(data, sizeof(data), multiCb, nullptr, &unknownPid);
  TEST_ASSERT_TRUE(hitUnknown);
  TEST_ASSERT_EQUAL_UINT8(0xFF, unknownPid);
  TEST_ASSERT_EQUAL_UINT8(1, g_capturedCount); // 0x0Cのみ。0xFF以降は読まれない
}

static void test_multi_response_reports_no_unknown_pid_when_all_known(void)
{
  uint8_t data[] = {0x41, 0x0C, 0x1A, 0xF8, 0x0D, 0x50};
  g_capturedCount = 0;
  uint8_t unknownPid = 0xAA; // 番兵値（書き換えられないことを確認）
  bool hitUnknown = obdParseMultiResponse(data, sizeof(data), multiCb, nullptr, &unknownPid);
  TEST_ASSERT_FALSE(hitUnknown);
  TEST_ASSERT_EQUAL_UINT8(0xAA, unknownPid);
}

// ─── 派生値計算（boost/燃費）────────────────────────────────────────────────

static void test_compute_derived_boost_uses_baro_when_available(void)
{
  OBDReading r = {};
  r.valid = true;
  r.mapKpa = 120;
  r.baroKpa = 100;
  obdComputeDerived(r);
  TEST_ASSERT_EQUAL_INT8(20, r.boostKpa);
}

static void test_compute_derived_boost_falls_back_to_standard_atmosphere(void)
{
  // baroKpa未取得(0)時は標準大気圧101kPaで代用
  OBDReading r = {};
  r.valid = true;
  r.mapKpa = 120;
  r.baroKpa = 0;
  obdComputeDerived(r);
  TEST_ASSERT_EQUAL_INT8(19, r.boostKpa); // 120-101
}

static void test_compute_derived_fuel_rate_from_maf(void)
{
  OBDReading r = {};
  r.valid = true;
  r.mafGs = 10.0f;
  obdComputeDerived(r);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 10.0f / (14.7f * 0.745f) * 3.6f, r.fuelRateLph);
}

static void test_compute_derived_fuel_rate_reflects_lambda(void)
{
  // commandedAfr(=λ)が0.5〜2.0の範囲内なら分母に反映する（暖機増量中λ<1の過小評価補正）
  OBDReading r = {};
  r.valid = true;
  r.mafGs = 10.0f;
  r.commandedAfr = 0.9f;
  obdComputeDerived(r);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 10.0f / (14.7f * 0.9f * 0.745f) * 3.6f, r.fuelRateLph);
}

static void test_compute_derived_fuel_rate_falls_back_to_lambda_one_when_out_of_range(void)
{
  // commandedAfrが0.5未満・2.0以上（未取得の0含む異常値）はλ=1にフォールバック
  OBDReading r = {};
  r.valid = true;
  r.mafGs = 10.0f;
  r.commandedAfr = 3.0f; // 異常値
  obdComputeDerived(r);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 10.0f / (14.7f * 0.745f) * 3.6f, r.fuelRateLph);
}

static void test_compute_derived_fuel_rate_unset_when_maf_absent(void)
{
  OBDReading r = {};
  r.valid = true;
  r.mafGs = 0;
  r.fuelRateLph = -1.0f; // 未変更を検出するための番兵値
  obdComputeDerived(r);
  TEST_ASSERT_EQUAL_FLOAT(-1.0f, r.fuelRateLph);
}

static void test_compute_derived_skips_when_invalid(void)
{
  // 応答なし(valid=false)時は派生値を計算しない
  OBDReading r = {};
  r.valid = false;
  r.mapKpa = 120;
  r.baroKpa = 100;
  r.boostKpa = -128; // 番兵値
  obdComputeDerived(r);
  TEST_ASSERT_EQUAL_INT8(-128, r.boostKpa);
}

int main(int argc, char **argv)
{
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_rpm_decodes_valid_response);
  RUN_TEST(test_rpm_rejects_wrong_pid);
  RUN_TEST(test_rpm_rejects_short_dlc);
  RUN_TEST(test_speed_decodes_valid_response);
  RUN_TEST(test_coolant_alt_decodes_when_sensor1_present);
  RUN_TEST(test_coolant_alt_rejects_when_sensor1_absent);
  RUN_TEST(test_charge_air_temp_decodes_both_sensors);
  RUN_TEST(test_charge_air_temp_decodes_sensor2_only);
  RUN_TEST(test_maf_alt_decodes_when_sensor1_present);
  RUN_TEST(test_maf_alt_rejects_when_sensor1_absent);
  RUN_TEST(test_multi_response_parses_two_known_pids);
  RUN_TEST(test_multi_response_stops_at_unknown_pid);
  RUN_TEST(test_multi_response_reports_no_unknown_pid_when_all_known);
  RUN_TEST(test_compute_derived_boost_uses_baro_when_available);
  RUN_TEST(test_compute_derived_boost_falls_back_to_standard_atmosphere);
  RUN_TEST(test_compute_derived_fuel_rate_from_maf);
  RUN_TEST(test_compute_derived_fuel_rate_reflects_lambda);
  RUN_TEST(test_compute_derived_fuel_rate_falls_back_to_lambda_one_when_out_of_range);
  RUN_TEST(test_compute_derived_fuel_rate_unset_when_maf_absent);
  RUN_TEST(test_compute_derived_skips_when_invalid);
  return UNITY_END();
}
