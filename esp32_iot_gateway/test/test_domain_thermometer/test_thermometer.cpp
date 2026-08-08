#include <unity.h>
#include <string>
#include "domain/thermometer.h"

void setUp(void) {}
void tearDown(void) {}

// WoIOSensor Manufacturer Data フォーマット（thermometer.h参照）:
//   [10]   温度小数部 (bit3-0)
//   [11]   温度整数部 (bit6-0) + 符号 (bit7: 1=正)
//   [12]   湿度 (%)
// Service Data フォーマット: [2]=バッテリー(%)

static void test_parse_common_decodes_positive_temperature(void)
{
  std::string mf(13, '\0');
  mf[10] = 0x05;               // 小数部 0.5
  mf[11] = (char)(0x80 | 25);  // 符号=正, 整数部25
  mf[12] = (char)60;           // 湿度60%
  std::string sd(3, '\0');
  sd[2] = (char)80; // バッテリー80%

  ThermometerData d = ThermometerParser::parse("AA:BB:CC:DD:EE:FF", -60, mf, sd);

  TEST_ASSERT_TRUE(d.parsed);
  TEST_ASSERT_EQUAL_FLOAT(25.5f, d.temp);
  TEST_ASSERT_EQUAL_UINT8(60, d.humidity);
  TEST_ASSERT_EQUAL_UINT8(80, d.battery);
}

static void test_parse_common_decodes_negative_temperature(void)
{
  std::string mf(13, '\0');
  mf[10] = 0x00;         // 小数部 0.0
  mf[11] = (char)10;     // 符号=負(bit7=0), 整数部10
  mf[12] = (char)45;
  std::string sd;

  ThermometerData d = ThermometerParser::parse("AA:BB:CC:DD:EE:FF", -60, mf, sd);

  TEST_ASSERT_TRUE(d.parsed);
  TEST_ASSERT_EQUAL_FLOAT(-10.0f, d.temp);
}

static void test_parse_common_skips_when_manufacturer_data_too_short(void)
{
  std::string mf(5, '\0'); // 13バイト未満なので温度は未パース
  std::string sd;

  ThermometerData d = ThermometerParser::parse("AA:BB:CC:DD:EE:FF", 0, mf, sd);

  TEST_ASSERT_FALSE(d.parsed);
}

static void test_mfhex_encodes_short_manufacturer_data(void)
{
  std::string mf = {(char)0x01, (char)0x02, (char)0x03};
  std::string sd;

  ThermometerData d = ThermometerParser::parse("AA:BB:CC:DD:EE:FF", 0, mf, sd);

  TEST_ASSERT_EQUAL_STRING("010203", d.mfHex);
}

// mfHex[40] は19バイト分（38 hex文字+null終端）までしか安全に保持できない。
// 19バイトちょうど（境界値）は現行コードでもオーバーフローしない。
static void test_mfhex_holds_exactly_19_bytes_at_boundary(void)
{
  std::string mf(19, '\0');
  for (size_t i = 0; i < mf.size(); i++)
    mf[i] = (char)(i + 1);
  std::string sd;

  ThermometerData d = ThermometerParser::parse("AA:BB:CC:DD:EE:FF", 0, mf, sd);

  TEST_ASSERT_EQUAL_STRING("0102030405060708090a0b0c0d0e0f10111213", d.mfHex);
}

// 20バイト以上のManufacturer Data（未知のセンサー種別・不正/悪意あるアドバタイズ等）を
// 受け取った場合でも、mfHex[40]の外へ書き込んではいけない。
// 既知バグ: 現行コードはhexBytesの上限がsizeof(mfHex)/2=20になっており、
// 20バイト目の処理でmfHex[40]（配列外）にNUL終端を書き込むoff-by-one overflowがある。
// ASan有効化のため、修正前はここでstack-buffer-overflowを検出してクラッシュする。
static void test_mfhex_does_not_overflow_on_long_manufacturer_data(void)
{
  std::string mf(25, '\0');
  for (size_t i = 0; i < mf.size(); i++)
    mf[i] = (char)(i + 1);
  std::string sd;

  ThermometerData d = ThermometerParser::parse("AA:BB:CC:DD:EE:FF", 0, mf, sd);

  // 19バイト分だけ保持し、20バイト目以降は安全に切り捨てられること
  TEST_ASSERT_EQUAL_STRING("0102030405060708090a0b0c0d0e0f10111213", d.mfHex);
}

int main(int argc, char **argv)
{
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_parse_common_decodes_positive_temperature);
  RUN_TEST(test_parse_common_decodes_negative_temperature);
  RUN_TEST(test_parse_common_skips_when_manufacturer_data_too_short);
  RUN_TEST(test_mfhex_encodes_short_manufacturer_data);
  RUN_TEST(test_mfhex_holds_exactly_19_bytes_at_boundary);
  RUN_TEST(test_mfhex_does_not_overflow_on_long_manufacturer_data);
  return UNITY_END();
}
