#include "mode_common.h"
#include "mode_context.h"
#include "monitor.h"
#include "jobs.h"
#include "command.h"
#include "ota.h"
#include "shadow.h"
#include "pubqueue.h"
#include "obdpoll.h"
#include "../device/ble_scan.h"
#include "../device/ble_peripheral.h"
#include "../device/lte.h"
#include "../device/can.h"
#include "../device/oled.h"
#include "../domain/charging.h"
#include "../board_pins.h"
#include "../config.h"
#include "../logger.h"
#include <Arduino.h>
#include <string.h>
#include <time.h>
#include <esp_sleep.h>
#include <driver/gpio.h>
#include <driver/rtc_io.h>

void updateChargingState()
{
  float vMain = modeCtx.lastResult().reading.main.voltage;
  float vSub = modeCtx.lastResult().reading.sub.voltage;
  ChargingThresholds th{getChgStartV(), getChgStopV(), getChgMinDiffV()};

  bool wasCharging = isCharging();
  bool shouldCharge = decideCharging(vMain, vSub, wasCharging, th);
  if (shouldCharge == wasCharging)
    return;

  setCharging(shouldCharge);
  digitalWrite(boardPins().chgOnPin, shouldCharge ? HIGH : LOW);
  float diff = vSub - vMain;
  if (shouldCharge)
    logger.printf("[MAIN] auto charge ON  vMain=%.2fV < startV=%.2fV diff=%.2fV\n", vMain, th.startV, diff);
  else
    logger.printf("[MAIN] auto charge OFF vMain=%.2fV stopV=%.2fV diff=%.2fV minDiff=%.2fV\n",
                  vMain, th.stopV, diff, th.minDiff);
}

uint32_t secsToNextBoundary()
{
  time_t now = time(nullptr);
  if (now <= 1577836800L) // 2020-01-01以前なら時刻未同期
    return SLEEP_INTERVAL_SEC;
  time_t next = ((now / (time_t)SLEEP_INTERVAL_SEC) + 1) * (time_t)SLEEP_INTERVAL_SEC;
  return (uint32_t)(next - now);
}

void pollBleCollect()
{
  if (!modeCtx.blePending())
    return;
  MeasureResult r = modeCtx.lastResult();
  if (!collectBle(r))
    return;
  modeCtx.setLastResult(r);
  publishBle(r);
  modeCtx.setBlePending(false);
}

void checkAndHandleJob()
{
  JobInfo job;
  if (jobsGetNext(job))
  {
    if (strcmp(job.operation, "ota") == 0)
      ota.handleJob(job); // 成功時は esp_restart() するため戻らない
    else
      commandHandleJob(job);
  }
}

bool detectContinuousPromotionTrigger(bool &viaBle, uint32_t bleWaitSec)
{
  if (blePeripheral.isConnected())
  {
    viaBle = true;
    return true;
  }

  // CAN生存確認は応答の有無しか使わないため、obdPoll()の29PID+DID調査ではなく
  // 軽量な単一PIDチェックを使う（CAN bus負荷・所要時間を抑える）
  if (obdCheckCanAlive())
  {
    viaBle = false;
    return true;
  }

  unsigned long waitStart = millis();
  while (millis() - waitStart < bleWaitSec * 1000UL)
  {
    if (blePeripheral.isConnected())
    {
      viaBle = true;
      return true;
    }
    delay(100);
  }
  return false;
}

void finishCycleAndPowerDown()
{
  updateChargingState();
  shadowPublishConfig();
  shadowPollDelta();
#ifndef DEBUG_SKIP_NETWORK
  // このサイクルのBLEスキャン結果を同サイクル内でflush()できるよう、非同期スキャンの完了を待って収集する。
  // NimBLEスタック異常時に無限待機しないようSCAN_TIME+マージンで打ち切り、強制停止する
  if (modeCtx.blePending())
  {
    MeasureResult r = modeCtx.lastResult();
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
    modeCtx.setLastResult(r);
    publishBle(r);
    modeCtx.setBlePending(false);
  }

  // publishBle()はloop()側のflush()より後に呼ばれるため、ここでflush()しないと
  // このサイクルのBLEデータが未送信のまま次サイクルまで1周分遅延してしまう
  queue.flush();
  queue.save();
  delay(1500); // SIM7080G の TCP 送信バッファをフラッシュさせてから切断（このflush()がサイクル最後のpublishのため必須）
  lte.disconnect();
  lte.radioOff();
#endif
  canDeinit(); // CANはDeepSleep突入時のみ停止する（それ以外は常時起動しておく）
  oledClear();
}

// ESP32-S3モジュール内蔵のBOOTボタン。DeepSleep復帰用のEXT0 wakeupソース。
// board_pins.hが管理する基板固有ピンとは異なりモジュール側で固定のため、ここで#define する
#define WAKE_PIN GPIO_NUM_0

void enterDeepSleepFor(uint32_t sec)
{
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
  esp_sleep_enable_timer_wakeup((uint64_t)sec * 1000000ULL);
  esp_deep_sleep_start();
}
