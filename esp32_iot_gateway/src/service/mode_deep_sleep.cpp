#include "mode_deep_sleep.h"
#include "mode_context.h"
#include "mode_common.h"
#include "monitor.h"
#include "shadow.h"
#include "pubqueue.h"
#include "../device/ble_peripheral.h"
#include "../device/ble_scan.h"
#include "../device/lte.h"
#include "../device/can.h"
#include "../device/oled.h"
#include "../board_pins.h"
#include "../config.h"
#include "../logger.h"
#include <Arduino.h>
#include <esp_sleep.h>
#include <driver/gpio.h>
#include <driver/rtc_io.h>

DeepSleepModeHandler deepSleepMode(modeCtx);

// ESP32-S3モジュール内蔵のBOOTボタン。DeepSleep復帰用のEXT0 wakeupソース。
// board_pins.hが管理する基板固有ピンとは異なりモジュール側で固定のため、ここで#define する。
// DeepSleepModeHandler::run() でのみ使用するため、このファイルに閉じる
#define WAKE_PIN GPIO_NUM_0

// BLE 接続 → CONTINUOUS 昇格。未接続なら DeepSleep 突入前に BLE_WAKE_WINDOW_SEC 秒だけ
// 接続を待つ（起床直後の setup() 中もアドバタイズ済みのため、実際の待受はそれより長い）。
// _ctx.userForcedSleep() 中は BTN1 での DEEP_SLEEP 選択を BLE 接続中でも尊重し、自動昇格させない。
// BLEが切断されている場合はここでuserForcedSleepフラグをリセットし、次回接続時は通常通り自動昇格させる
void DeepSleepModeHandler::beforeRun()
{
  if (!blePeripheral.isConnected())
    _ctx.setUserForcedSleep(false);

  if (_ctx.userForcedSleep())
    return;

  if (blePeripheral.isConnected())
  {
    _ctx.setMode(OperationMode::CONTINUOUS);
    _ctx.setBleUpgradedToContinuous(true);
    return;
  }

  unsigned long waitStart = millis();
  while (millis() - waitStart < BLE_WAKE_WINDOW_SEC * 1000UL)
  {
    if (blePeripheral.isConnected())
    {
      _ctx.setMode(OperationMode::CONTINUOUS);
      _ctx.setBleUpgradedToContinuous(true);
      break;
    }
    delay(100);
  }
}

// shadow 同期 → LTE 切断 → DeepSleep（戻らない）
void DeepSleepModeHandler::run()
{
  updateChargingState();
  shadowPublishConfig();
  shadowPollDelta();
  delay(1500); // SIM7080G の TCP 送信バッファをフラッシュさせてから切断
#ifndef DEBUG_SKIP_NETWORK
  // このサイクルのBLEスキャン結果を同サイクル内でflush()できるよう、非同期スキャンの完了を待って収集する。
  // NimBLEスタック異常時に無限待機しないようSCAN_TIME+マージンで打ち切り、強制停止する
  if (_ctx.blePending())
  {
    MeasureResult r = _ctx.lastResult();
    unsigned long bleWaitStart = millis();
    const unsigned long bleWaitTimeoutMs = (SCAN_TIME + 3) * 1000UL;
    while (!collectBle(r) && millis() - bleWaitStart < bleWaitTimeoutMs)
      delay(50);
    if (bleScanner.isScanning())
    {
      logger.println("[MAIN] BLEスキャン完了待ちタイムアウト → 強制停止しこの周期はBLEデータなしで続行");
      bleScanner.stop();
      collectBle(r);
    }
    _ctx.setLastResult(r);
    publishBle(r);
    _ctx.setBlePending(false);
  }

  // publishBle()はloop()側のflush()より後に呼ばれるため、ここでflush()しないと
  // このサイクルのBLEデータが未送信のまま次サイクルまで1周分遅延してしまう
  queue.flush();
  queue.save();
  lte.disconnect();
  lte.radioOff();
#endif
  canDeinit(); // CANはDeepSleep突入時のみ停止する（それ以外は常時起動しておく）
  oledClear();

  uint32_t sleepSec = secsToNextBoundary();
  if (isCharging())
    gpio_hold_en((gpio_num_t)boardPins().chgOnPin);
#if BOARD_VERSION == 2
  gpio_hold_en((gpio_num_t)boardPins().pwrHoldPin);
#endif
  rtc_gpio_init(WAKE_PIN);
  rtc_gpio_set_direction(WAKE_PIN, RTC_GPIO_MODE_INPUT_ONLY);
  rtc_gpio_pullup_en(WAKE_PIN);
  rtc_gpio_pulldown_dis(WAKE_PIN);
  esp_sleep_enable_ext0_wakeup(WAKE_PIN, 0);
  esp_sleep_enable_timer_wakeup((uint64_t)sleepSec * 1000000ULL);
  esp_deep_sleep_start();
}
