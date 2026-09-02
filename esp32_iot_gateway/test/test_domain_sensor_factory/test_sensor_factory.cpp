#include <unity.h>
#include <string>
#include "domain/sensor_factory.h"

void setUp(void) {}
void tearDown(void) {}

// serviceData[0] == 0x35 → Co2MeterParser、それ以外 → ThermometerParser（sensor_factory.h参照）

static void test_parse_dispatches_to_co2_meter_when_service_data_type_matches(void)
{
  std::string mf(17, '\0');
  std::string sd(1, '\0');
  sd[0] = (char)0x35;

  SensorVariant v = SensorParserFactory::parse("AA:BB:CC:DD:EE:FF", 0, mf, sd);

  TEST_ASSERT_TRUE(std::holds_alternative<Co2MeterData>(v));
}

static void test_parse_dispatches_to_thermometer_when_service_data_type_differs(void)
{
  std::string mf(13, '\0');
  std::string sd(1, '\0');
  sd[0] = (char)0x00; // WoIOSensor（温湿度計）のステータスバイト等、0x35以外

  SensorVariant v = SensorParserFactory::parse("AA:BB:CC:DD:EE:FF", 0, mf, sd);

  TEST_ASSERT_TRUE(std::holds_alternative<ThermometerData>(v));
}

static void test_parse_dispatches_to_thermometer_when_service_data_empty(void)
{
  std::string mf(13, '\0');
  std::string sd; // 空 → sd.length()>=1 を満たさないためCO2判定に入らない

  SensorVariant v = SensorParserFactory::parse("AA:BB:CC:DD:EE:FF", 0, mf, sd);

  TEST_ASSERT_TRUE(std::holds_alternative<ThermometerData>(v));
}

int main(int argc, char **argv)
{
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_parse_dispatches_to_co2_meter_when_service_data_type_matches);
  RUN_TEST(test_parse_dispatches_to_thermometer_when_service_data_type_differs);
  RUN_TEST(test_parse_dispatches_to_thermometer_when_service_data_empty);
  return UNITY_END();
}
