#include "mode_deep_sleep.h"
#include "mode_context.h"
#include "mode_common.h"
#include "../device/ble_peripheral.h"
#include "../config.h"
#include <Arduino.h>

DeepSleepModeHandler deepSleepMode(modeCtx);

// BLE 接続 または CAN 応答（IGN ON 相当）→ CONTINUOUS 昇格。検知ロジック自体は
// mode_common.cpp の detectContinuousPromotionTrigger() に共通化されている
// （LIGHT_SLEEPの短周期ゲートも同じ関数を使う）。
// _ctx.userForcedSleep() 中は BTN1 での DEEP_SLEEP 選択を BLE 接続中でも尊重し、自動昇格させない。
// BLEが切断されている場合はここでuserForcedSleepフラグをリセットし、次回接続時は通常通り自動昇格させる
void DeepSleepModeHandler::beforeRun()
{
  if (!blePeripheral.isConnected())
    _ctx.setUserForcedSleep(false);

  if (_ctx.userForcedSleep())
    return;

  bool viaBle = false;
  if (!detectContinuousPromotionTrigger(viaBle, BLE_WAKE_WINDOW_SEC))
    return;

  _ctx.setMode(OperationMode::CONTINUOUS);
  _ctx.setPromotedFromMode(OperationMode::DEEP_SLEEP);
  if (viaBle)
  {
    _ctx.setBleUpgradedToContinuous(true);
  }
  else
  {
    _ctx.setCanUpgradedToContinuous(true);
  }
}

// shadow 同期 → LTE 切断 → DeepSleep（戻らない）
void DeepSleepModeHandler::run()
{
  finishCycleAndPowerDown(); // shadow同期・BLE収集/publish・queue flush/save・LTE off・CAN off・OLED clear（mode_common.cpp）

  enterDeepSleepFor(secsToNextBoundary()); // GPIO hold・起床設定・esp_deep_sleep_start()（mode_common.cpp、戻らない）
}
