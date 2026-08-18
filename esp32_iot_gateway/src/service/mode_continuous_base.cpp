#include "mode_continuous_base.h"
#include "mode_context.h"
#include "mode_common.h"
#include "menu.h"
#include "shadow.h"
#include "obdpoll.h"
#include "../device/button.h"
#include "../device/oled.h"
#include "../device/ble_peripheral.h"
#include "../device/ads.h"
#include "../device/ina228.h"
#include "../device/lte.h"
#include "../config.h"
#include "../logger.h"
#include <Arduino.h>
#include <time.h>

// BLE 切断 → DEEP_SLEEP に戻す（BLE 接続で昇格した場合のみ）
void ContinuousModeHandlerBase::beforeRun()
{
  if (_ctx.bleUpgradedToContinuous() && !blePeripheral.isConnected())
  {
    _ctx.setMode(OperationMode::DEEP_SLEEP);
    _ctx.setBleUpgradedToContinuous(false);
  }
}

void ContinuousModeHandlerBase::handleMenuButton()
{
  _ctx.setMode(enterMenuMode());
  if (_ctx.mode() == OperationMode::CONTINUOUS && blePeripheral.isConnected())
    _ctx.setBleUpgradedToContinuous(true);
}

const char *ContinuousModeHandlerBase::nextModeLabel() const
{
  if (selfMode() == OperationMode::TIMED_CONTINUOUS && time(nullptr) >= _ctx.continuousUntilEpoch())
    return "DEEP_SLEEP"; // TIMED_CONTINUOUSの期限到達で次回DEEP_SLEEPへ戻る
  if (_ctx.userForcedSleep())
    return "DEEP_SLEEP"; // BTN1長押しでBLE接続中に強制スリープ選択済み
  if (_ctx.bleUpgradedToContinuous() && !blePeripheral.isConnected())
    return "DEEP_SLEEP"; // BLEで昇格したがBLEが切れているため次回で自動的に戻る
  return "CONTINUOUS";
}

void ContinuousModeHandlerBase::onTick()
{
  OBDReading r = obdPoll();
  blePeripheral.notifyObd(r);
}

// 次の5分境界（UTC）まで待機しながらボタン監視・カウントダウン表示・BLE Notify
void ContinuousModeHandlerBase::continuousLoopCore()
{
  unsigned long waitMs = (unsigned long)secsToNextBoundary() * 1000UL;
  unsigned long waitStart = millis();
  unsigned long lastNotify = 0;
  int lastRemain = -1;

  // _ctx.mode()がselfMode()から変わった時点で即座に抜け、modeManagerへ制御を返す
  // （そのままだと最大waitMs経過するまでモード変更が実際には反映されない）
  while (millis() - waitStart < waitMs && _ctx.mode() == selfMode())
  {
    ButtonEvent ev = button.read();
    if (ev == ButtonEvent::BTN0_SHORT)
    {
      handleMenuButton();
      int curRemain = (int)((waitMs - (millis() - waitStart)) / 1000);
      oledShowSensorData(_ctx.lastResult().reading);
      oledUpdateCountdown(curRemain, nextModeLabel());
      lastRemain = curRemain;
    }
    if (ev == ButtonEvent::BTN1_LONG)
    {
      if (_ctx.mode() == OperationMode::DEEP_SLEEP)
      {
        logger.println("[MAIN] BTN1 長押し → CONTINUOUS モードへ切り替え");
        oledPrint("Switching continuous...");
        _ctx.setMode(OperationMode::CONTINUOUS);
      }
      else if (_ctx.mode() == selfMode())
      {
        logger.println("[MAIN] BTN1 長押し → DEEP_SLEEP モードへ切り替え");
        oledPrint("Switching sleep...");
        _ctx.setMode(OperationMode::DEEP_SLEEP);
        _ctx.setBleUpgradedToContinuous(false);
        // BLE接続中の手動切り替えは、loop()側の自動昇格に即座に上書きされてしまうため
        // (BLE接続 && DEEP_SLEEP の条件が真になる)、切断されるまでは尊重する
        if (blePeripheral.isConnected())
          _ctx.setUserForcedSleep(true);
      }
    }

    int remain = (int)((waitMs - (millis() - waitStart)) / 1000);
    if (remain != lastRemain)
    {
      oledUpdateCountdown(remain, nextModeLabel());
      lastRemain = remain;
    }

    unsigned long now = millis();
    if (now - lastNotify >= 1000)
    {
      lastNotify = now;
      updateChargingState();
      pollBleCollect(); // measure()で開始した非同期BLEスキャンの完了をここで拾う

#ifndef DEBUG_SKIP_NETWORK
      // 継続モード中は5分待機ループに留まり続けるため、1秒ティックでもShadow deltaを確認する
      shadowPollDelta();
#endif

      onTick();

      blePeripheral.notify(
          adsReadDiffMain(),
          ina228.readCurrent(),
          ina228.readPower(),
          adsReadDiffSub(),
          ina228.readTemp(),
          ina228.readCharge() + (float)getAhOffset(),
          (uint32_t)time(nullptr),
          lte.isConnected());
    }

    delay(50);
  }
}
