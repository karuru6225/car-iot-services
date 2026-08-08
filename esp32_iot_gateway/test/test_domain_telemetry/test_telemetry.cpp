#include <unity.h>
#include <string>
#include <cstring>
#include "domain/telemetry.h"
#include "config.h"

extern void testSetAhOffset(int32_t v);
extern void testSetChgStartV(float v);
extern void testSetChgStopV(float v);
extern void testSetChgMinDiffV(float v);
extern void testSetDebugLogEnabled(bool v);

void setUp(void)
{
  testSetAhOffset(0);
  testSetChgStartV(12.0f);
  testSetChgStopV(14.0f);
  testSetChgMinDiffV(0.3f);
  testSetDebugLogEnabled(false);
  setCharging(false);
}
void tearDown(void) {}

// clearDesiredのtrue/false両ブランチで同じフィールド一式が出ていることを確認する。
// telemetry.cppはこの2ブランチをsnprintfで手書きコピペしており、片方だけフィールドを
// 足し忘れる/消し忘れるドリフトが起きやすい（コードレビューで指摘済み）ため、
// 「両方に全フィールドが揃っている」ことをテストで縛る。
static const char *kReportedFields[] = {
    "\"ah_offset\"", "\"chg_start_v\"", "\"chg_stop_v\"", "\"chg_min_diff_v\"",
    "\"debug_log\"", "\"charging\"", "\"override_next_mode\"", "\"fw_version\"",
};

static void assertHasAllReportedFields(const char *json)
{
  for (const char *f : kReportedFields)
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(json, f), f);
}

static void test_build_config_payload_without_clear_desired_has_no_desired_key(void)
{
  char buf[256];
  int len = buildConfigPayload(buf, sizeof(buf), false, nullptr);
  TEST_ASSERT_GREATER_THAN(0, len);
  assertHasAllReportedFields(buf);
  TEST_ASSERT_NULL(strstr(buf, "\"desired\""));
}

static void test_build_config_payload_with_clear_desired_adds_desired_null(void)
{
  char buf[256];
  int len = buildConfigPayload(buf, sizeof(buf), true, nullptr);
  TEST_ASSERT_GREATER_THAN(0, len);
  assertHasAllReportedFields(buf);
  TEST_ASSERT_NOT_NULL(strstr(buf, "\"desired\":null"));
}

static void test_build_config_payload_reports_override_next_mode_as_quoted_string(void)
{
  char buf[256];
  buildConfigPayload(buf, sizeof(buf), false, "one_shot_continuous");
  TEST_ASSERT_NOT_NULL(strstr(buf, "\"override_next_mode\":\"one_shot_continuous\""));
}

static void test_build_config_payload_reports_override_next_mode_null_by_default(void)
{
  char buf[256];
  buildConfigPayload(buf, sizeof(buf), false, nullptr);
  TEST_ASSERT_NOT_NULL(strstr(buf, "\"override_next_mode\":null"));
}

static void test_build_config_payload_reflects_current_config_values(void)
{
  testSetAhOffset(-42);
  testSetChgStartV(11.5f);
  setCharging(true);

  char buf[256];
  buildConfigPayload(buf, sizeof(buf), false, nullptr);

  TEST_ASSERT_NOT_NULL(strstr(buf, "\"ah_offset\":-42"));
  TEST_ASSERT_NOT_NULL(strstr(buf, "\"chg_start_v\":11.50"));
  TEST_ASSERT_NOT_NULL(strstr(buf, "\"charging\":true"));
}

// ─── ITelemetryEncoder（JSON版のみ。MsgPack版はesp_rom_crc32_leがESP-IDF依存で対象外）───

static void test_json_encoder_encodes_battery(void)
{
  JsonTelemetryEncoder enc;
  VoltageReading mainV{12.3f};
  VoltageReading subV{12.6f};
  PowerReading pwr{1.5f, 18.0f, 25.0f, 3.2f};

  uint8_t buf[256];
  size_t len = enc.encodeBattery(buf, sizeof(buf), mainV, subV, pwr, (time_t)1700000000);

  TEST_ASSERT_GREATER_THAN(0, (int)len);
  std::string json((char *)buf, len);
  TEST_ASSERT_NOT_NULL(strstr(json.c_str(), "\"t\":\"battery\""));
  TEST_ASSERT_NOT_NULL(strstr(json.c_str(), "\"ah\":3.2"));
}

static void test_json_encoder_encodes_thermometer(void)
{
  JsonTelemetryEncoder enc;
  ThermometerData d = {};
  strncpy(d.address, "AA:BB:CC:DD:EE:FF", sizeof(d.address) - 1);
  d.temp = 21.5f;
  d.humidity = 40;

  uint8_t buf[256];
  size_t len = enc.encodeThermometer(buf, sizeof(buf), d);

  std::string json((char *)buf, len);
  TEST_ASSERT_NOT_NULL(strstr(json.c_str(), "\"t\":\"thermometer\""));
  TEST_ASSERT_NOT_NULL(strstr(json.c_str(), "\"a\":\"AA:BB:CC:DD:EE:FF\""));
}

static void test_json_encoder_topic_suffix(void)
{
  JsonTelemetryEncoder enc;
  TEST_ASSERT_EQUAL_STRING("data", enc.topicSuffix());
}

int main(int argc, char **argv)
{
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_build_config_payload_without_clear_desired_has_no_desired_key);
  RUN_TEST(test_build_config_payload_with_clear_desired_adds_desired_null);
  RUN_TEST(test_build_config_payload_reports_override_next_mode_as_quoted_string);
  RUN_TEST(test_build_config_payload_reports_override_next_mode_null_by_default);
  RUN_TEST(test_build_config_payload_reflects_current_config_values);
  RUN_TEST(test_json_encoder_encodes_battery);
  RUN_TEST(test_json_encoder_encodes_thermometer);
  RUN_TEST(test_json_encoder_topic_suffix);
  return UNITY_END();
}
