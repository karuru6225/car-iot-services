#include <unity.h>
#include <string>
#include "domain/co2meter.h"

void setUp(void) {}
void tearDown(void) {}

// CO2センサー Manufacturer Data フォーマット（co2meter.h参照）:
//   [10-12] 温度/湿度 ← ThermometerParser::parseCommon と共通
//   [15-16] CO2 (ppm, big-endian uint16)

static void test_parse_decodes_co2_and_shares_common_fields(void)
{
  std::string mf(17, '\0');
  mf[10] = 0x00;
  mf[11] = (char)(0x80 | 22); // 符号=正, 22℃
  mf[12] = (char)55;          // 湿度55%
  mf[15] = (char)0x02;        // CO2 = 0x0203 = 515ppm
  mf[16] = (char)0x03;
  std::string sd(3, '\0');
  sd[0] = (char)0x35; // CO2センサー種別
  sd[2] = (char)90;   // バッテリー90%

  Co2MeterData d = Co2MeterParser::parse("11:22:33:44:55:66", -50, mf, sd);

  TEST_ASSERT_TRUE(d.parsed);
  TEST_ASSERT_EQUAL_FLOAT(22.0f, d.temp);
  TEST_ASSERT_EQUAL_UINT8(55, d.humidity);
  TEST_ASSERT_EQUAL_UINT8(90, d.battery);
  TEST_ASSERT_EQUAL_UINT16(515, d.co2);
  TEST_ASSERT_EQUAL_STRING("11:22:33:44:55:66", d.address);
  TEST_ASSERT_EQUAL_INT8(-50, d.rssi);
}

static void test_parse_leaves_co2_zero_when_manufacturer_data_too_short(void)
{
  std::string mf(13, '\0'); // 17バイト未満なのでCO2フィールドに未到達
  std::string sd;

  Co2MeterData d = Co2MeterParser::parse("11:22:33:44:55:66", 0, mf, sd);

  TEST_ASSERT_EQUAL_UINT16(0, d.co2);
}

int main(int argc, char **argv)
{
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_parse_decodes_co2_and_shares_common_fields);
  RUN_TEST(test_parse_leaves_co2_zero_when_manufacturer_data_too_short);
  return UNITY_END();
}
