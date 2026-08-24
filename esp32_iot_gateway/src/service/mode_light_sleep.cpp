#include "mode_light_sleep.h"
#include "mode_context.h"
#include "mode_common.h"
#include "../device/can.h"
#include "../config.h"
#include "../logger.h"
#include <Arduino.h>
#include <esp_sleep.h>

LightSleepModeHandler lightSleepMode(modeCtx);

namespace
{
// LIGHT_SLEEPの短周期ピーク中かどうか。deep sleepを跨いで保持する必要があるためRTC_DATA_ATTR
RTC_DATA_ATTR bool s_lightSleepPeeking = false;
// 次の起床が「5分境界到達」を意図したものかどうか。secsToNextBoundary()は常に1〜300を返し
// 0にならないため、起床時の戻り値だけでは境界到達を判定できない。そのため寝る前に予約しておく
// （mode_common.cpp:secsToNextBoundary()参照）
RTC_DATA_ATTR bool s_lightSleepBoundaryWake = false;
} // namespace

void lightSleepShortWakeGate()
{
  if (esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_TIMER)
  {
    // BOOTボタン(EXT0)での強制起床やesp_restart()（OTA成功時等）でのリブート。
    // LIGHT_SLEEPの短周期ピークを放棄し、通常の起動フローに委ねる。ここでクリアしておかないと、
    // 通常フローがDEEP_SLEEPへ戻った後の次のタイマー起床でこのフラグが誤って生き残ってしまう
    s_lightSleepPeeking = false;
    s_lightSleepBoundaryWake = false;
    return;
  }
  if (!s_lightSleepPeeking)
    return;

  if (s_lightSleepBoundaryWake)
  {
    s_lightSleepBoundaryWake = false;
    logger.println("[LIGHT_SLEEP] 5分境界に到達 → 通常サイクルへ");
    modeCtx.setMode(OperationMode::LIGHT_SLEEP);
    return;
  }

  bool viaBle = false;
  if (detectContinuousPromotionTrigger(viaBle, LIGHT_SLEEP_BLE_WAIT_SEC))
  {
    s_lightSleepPeeking = false;
    logger.printf("[LIGHT_SLEEP] %s検知 → CONTINUOUSへ昇格\n", viaBle ? "BLE接続" : "CAN応答");
    modeCtx.setMode(OperationMode::CONTINUOUS);
    modeCtx.setPromotedFromMode(OperationMode::LIGHT_SLEEP);
    if (viaBle)
    {
      modeCtx.setBleUpgradedToContinuous(true);
    }
    else
    {
      modeCtx.setCanUpgradedToContinuous(true);
    }
    return;
  }

  uint32_t remain = secsToNextBoundary();
  canDeinit();
  if (remain > LIGHT_SLEEP_PEEK_INTERVAL_SEC)
  {
    enterDeepSleepFor(LIGHT_SLEEP_PEEK_INTERVAL_SEC); // 戻らない（mode_common.cpp）
  }
  else
  {
    // 次のピーク間隔ぶん丸々寝ると境界を跨いでしまうため、境界ちょうどのタイミングで起きるようにする
    s_lightSleepBoundaryWake = true;
    enterDeepSleepFor(remain); // 戻らない（mode_common.cpp）
  }
}

void LightSleepModeHandler::run()
{
  finishCycleAndPowerDown(); // shadow同期・BLE収集/publish・queue flush/save・LTE off・CAN off・OLED clear（mode_common.cpp）

  s_lightSleepPeeking = true;
  s_lightSleepBoundaryWake = false;
  enterDeepSleepFor(LIGHT_SLEEP_PEEK_INTERVAL_SEC); // 戻らない（mode_common.cpp）
}
