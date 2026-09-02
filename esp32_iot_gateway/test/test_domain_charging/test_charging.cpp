#include <unity.h>
#include "domain/charging.h"

void setUp(void) {}
void tearDown(void) {}

static const ChargingThresholds kTh{11.7f, 12.5f, 0.3f}; // startV, stopV, minDiff

static void test_turns_on_when_below_start_voltage_with_enough_diff(void)
{
  // vMain=11.5V(<startV) sub-main=0.5V(>=minDiff)
  bool result = decideCharging(11.5f, 12.0f, false, kTh);
  TEST_ASSERT_TRUE(result);
}

static void test_stays_off_when_diff_insufficient(void)
{
  // vMain=11.5V(<startV) だが sub-main=0.1V(<minDiff)
  bool result = decideCharging(11.5f, 11.6f, false, kTh);
  TEST_ASSERT_FALSE(result);
}

static void test_stays_off_when_vmain_not_below_start(void)
{
  // vMain=11.8V(>=startV)
  bool result = decideCharging(11.8f, 12.5f, false, kTh);
  TEST_ASSERT_FALSE(result);
}

static void test_turns_off_when_vmain_reaches_stop_voltage(void)
{
  // vMain=12.6V(>=stopV)
  bool result = decideCharging(12.6f, 12.8f, true, kTh);
  TEST_ASSERT_FALSE(result);
}

static void test_turns_off_when_diff_drops_below_min(void)
{
  // vMain=12.0V(<stopV) だが sub-main=0.2V(<minDiff)
  bool result = decideCharging(12.0f, 12.2f, true, kTh);
  TEST_ASSERT_FALSE(result);
}

static void test_stays_on_when_neither_stop_condition_met(void)
{
  // vMain=12.0V(<stopV) かつ sub-main=0.5V(>=minDiff)
  bool result = decideCharging(12.0f, 12.5f, true, kTh);
  TEST_ASSERT_TRUE(result);
}

static void test_stays_charging_unchanged_when_vmain_below_10v_while_on(void)
{
  // センサー異常/未接続相当（vMain<10V）は両方向とも状態を変えない
  bool result = decideCharging(5.0f, 12.0f, true, kTh);
  TEST_ASSERT_TRUE(result);
}

static void test_stays_off_when_vmain_below_10v_while_off(void)
{
  bool result = decideCharging(5.0f, 12.0f, false, kTh);
  TEST_ASSERT_FALSE(result);
}

int main(int argc, char **argv)
{
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_turns_on_when_below_start_voltage_with_enough_diff);
  RUN_TEST(test_stays_off_when_diff_insufficient);
  RUN_TEST(test_stays_off_when_vmain_not_below_start);
  RUN_TEST(test_turns_off_when_vmain_reaches_stop_voltage);
  RUN_TEST(test_turns_off_when_diff_drops_below_min);
  RUN_TEST(test_stays_on_when_neither_stop_condition_met);
  RUN_TEST(test_stays_charging_unchanged_when_vmain_below_10v_while_on);
  RUN_TEST(test_stays_off_when_vmain_below_10v_while_off);
  return UNITY_END();
}
