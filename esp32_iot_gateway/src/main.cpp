// esp32_iot_gateway
//
// 起動 → (BTN0 長押し) メニュー → LTE 接続 → OTA チェック → loop()
//
// loop() の動作モード:
//   DEEP_SLEEP  : measureAndPublish() → DeepSleep 5分（デフォルト本番動作）
//   CONTINUOUS  : measureAndPublish() → 5分待機 → 繰り返し（BTN1 長押しで DEEP_SLEEP に切り替え）
//
// デバッグモード: #define DEBUG_MODE を有効にするとデフォルトモードが CONTINUOUS になる

// #define DEBUG_MODE

#include <Arduino.h>
#include "config.h"
#include "device/lte.h"
#include "service/logger.h"
#include "service/ota.h"
#include "service/jobs.h"
#include "service/command.h"
#include "service/mqtt.h"
#include "service/monitor.h"
#include "service/shadow.h"

#include "device/speaker.h"
#include "device/oled.h"
#include "device/ads.h"
#include "device/ina228.h"
#include "device/ble_scan.h"
#include "device/button.h"

#include "domain/ble_targets.h"
#include "service/menu.h"
#include "service/pubqueue.h"

#include <esp_sleep.h>
#include <driver/gpio.h>

#define RELAY_0_PIN 11
#define RELAY_1_PIN 13
#define RELAY_2_PIN 15
#define UNITX_EN_PIN 9

// #define DEBUG_SKIP_NETWORK

#ifdef DEBUG_MODE
static OperationMode g_mode = OperationMode::CONTINUOUS;
#else
static OperationMode g_mode = OperationMode::DEEP_SLEEP;
#endif

static esp_sleep_wakeup_cause_t g_wakeupCause = ESP_SLEEP_WAKEUP_UNDEFINED;
static MeasureResult g_lastResult = {};

void setup()
{
  g_wakeupCause = esp_sleep_get_wakeup_cause();

  // 充電 sleep から復帰した場合は最優先で CHG_ON を落とす
  if (isChargingSleep()) {
    gpio_hold_dis((gpio_num_t)CHG_ON_PIN);
    pinMode(CHG_ON_PIN, OUTPUT);
    digitalWrite(CHG_ON_PIN, LOW);
    setChargingSleep(false);
  }

  logger.init();
  delay(1000);
  logger.printf("\n=== esp32_iot_gateway %s 起動 (wakeup=%d) ===\n",
                FIRMWARE_VERSION, (int)g_wakeupCause);

  oledInit();
  adsInit();
  ina228.init();
  oledPrint("FW: " FIRMWARE_VERSION);
  if (g_wakeupCause != ESP_SLEEP_WAKEUP_TIMER)
  {
    speakerInit();
    playMelody(bootStart);
  }
  button.begin();

#ifndef DEBUG_SKIP_NETWORK
  bleScanner.setup();
  bleTargets.load();
#endif

  // BTN0 を押しながら起動でメニューモードへ（LTE 未起動のままオフライン動作）
  delay(1300);
  if (button.isDown(0))
  {
    oledPrint("Menu Mode");
    g_mode = enterMenuMode();
  }

#ifndef DEBUG_SKIP_NETWORK
  oledPrint("LTE connecting...");
  lte.setup(); // LTE_EN ON → モデム初期化 → GPRS 接続 → 時刻同期

  queue.load();  // 電源投入時: SPIFFS → RTC メモリ（DeepSleep 復帰時は no-op）
  queue.flush(); // 前回バッファ分を即送信

  // 充電完了（remaining == 0 かつ jobId あり）なら SUCCEEDED 報告
  if (getChargeRemainingSec() == 0 && getChargeJobId()[0] != '\0') {
    jobsReport(getChargeJobId(), "SUCCEEDED");
    clearCharge();
  }

  ota.reportPendingJobResult();

  // MQTT 接続が確認できた場合のみ起動を確定（LTE 障害時はロールバックさせる）
  if (mqtt.isConnected())
    ota.confirmBoot();

  shadowSetup();
  shadowPublishConfig();
  shadowPollDelta(3000); // 起動時に pending な desired を即適用

  oledPrint("Job checking...");
  jobsSetup();
  JobInfo job;
  if (jobsGetNext(job))
  {
    if (strcmp(job.operation, "ota") == 0)
      ota.handleJob(job); // 成功時は esp_restart() するため戻らない
    else
      commandHandleJob(job);
  }
#endif

  logger.printf("[MAIN] 起動完了 mode=%s\n",
                g_mode == OperationMode::CONTINUOUS ? "CONTINUOUS" : "DEEP_SLEEP");

  if (g_wakeupCause != ESP_SLEEP_WAKEUP_TIMER)
    playMelody(boot);
  pinMode(RELAY_0_PIN, OUTPUT);
  pinMode(RELAY_1_PIN, OUTPUT);
  pinMode(RELAY_2_PIN, OUTPUT);
  pinMode(CHG_ON_PIN, OUTPUT);
}

static void updateRelayIndicator(int remainSec, RelayMode mode)
{
  if (mode != RelayMode::SLEEP_INDICATOR)
  {
    digitalWrite(RELAY_0_PIN, LOW);
    digitalWrite(RELAY_1_PIN, LOW);
    digitalWrite(RELAY_2_PIN, LOW);
    return;
  }

  // CONTEXT.md「ULP によるDeepSleepカウントダウンLED」の表に準拠
  // remain 240〜300: GPIO11●  GPIO13●  GPIO15●
  // remain 180〜240: GPIO11●  GPIO13●  GPIO15○
  // remain 120〜180: GPIO11●  GPIO13○  GPIO15○
  // remain  60〜120: GPIO11○  GPIO13○  GPIO15○
  // remain   0〜 60: 全点滅（1秒周期）
  if (remainSec < 60)
  {
    uint8_t blink = (remainSec % 2 == 0) ? HIGH : LOW;
    digitalWrite(RELAY_0_PIN, blink);
    digitalWrite(RELAY_1_PIN, blink);
    digitalWrite(RELAY_2_PIN, blink);
  }
  else
  {
    digitalWrite(RELAY_0_PIN, remainSec >= 120 ? HIGH : LOW);
    digitalWrite(RELAY_1_PIN, remainSec >= 180 ? HIGH : LOW);
    digitalWrite(RELAY_2_PIN, remainSec >= 240 ? HIGH : LOW);
  }
}

void loop()
{
#ifndef DEBUG_SKIP_NETWORK
  g_lastResult = measure();
  publish(g_lastResult);
  queue.flush();
  shadowPollDelta();
  oledShowSensorData(g_lastResult.reading);
#endif

  if (g_mode == OperationMode::DEEP_SLEEP)
  {
    delay(1500); // SIM7080G の TCP 送信バッファをフラッシュさせてから切断
#ifndef DEBUG_SKIP_NETWORK
    queue.save();
    lte.disconnect();
    lte.radioOff(); // LTE_EN LOW
#endif
    oledClear();

    if (getChargeRemainingSec() > 0)
    {
      // 充電 sleep: 残り時間を 1 サイクル分消費して寝る
      uint32_t remaining = getChargeRemainingSec();
      uint32_t sleepSec  = (remaining >= SLEEP_INTERVAL_SEC)
                             ? SLEEP_INTERVAL_SEC : remaining;
      setChargeRemainingSec(remaining - sleepSec);
      logger.printf("[MAIN] 充電 DeepSleep: %u sec (remaining after: %u)\n",
                    sleepSec, remaining - sleepSec);
      digitalWrite(CHG_ON_PIN, HIGH);
      gpio_hold_en((gpio_num_t)CHG_ON_PIN);
      setChargingSleep(true);
      esp_sleep_enable_timer_wakeup((uint64_t)sleepSec * 1000000ULL);
    }
    else
    {
      logger.println("[MAIN] DeepSleep へ移行");
      esp_sleep_enable_timer_wakeup((uint64_t)SLEEP_INTERVAL_SEC * 1000000ULL);
    }
    esp_deep_sleep_start();
  }

  // CONTINUOUS: SLEEP_INTERVAL_SEC 秒待機しながらボタン監視・カウントダウン表示
  RelayMode relayMode = getRelayMode();
  unsigned long waitStart = millis();
  int lastRemain = -1;
  while (millis() - waitStart < (uint32_t)SLEEP_INTERVAL_SEC * 1000)
  {
    ButtonEvent ev = button.read();
    if (ev == ButtonEvent::BTN0_SHORT)
    {
      g_mode = enterMenuMode();
      relayMode = getRelayMode();
      int curRemain = (int)((SLEEP_INTERVAL_SEC * 1000 - (millis() - waitStart)) / 1000);
      oledShowSensorData(g_lastResult.reading);
      oledUpdateCountdown(curRemain);
      updateRelayIndicator(curRemain, relayMode);
      lastRemain = curRemain;
    }
    if (ev == ButtonEvent::BTN1_LONG)
    {
      if (g_mode == OperationMode::DEEP_SLEEP)
      {
        logger.println("[MAIN] BTN1 長押し → CONTINUOUS モードへ切り替え");
        oledPrint("Switching continuous...");
        g_mode = OperationMode::CONTINUOUS;
      }
      else if (g_mode == OperationMode::CONTINUOUS)
      {
        logger.println("[MAIN] BTN1 長押し → DEEP_SLEEP モードへ切り替え");
        oledPrint("Switching sleep...");
        g_mode = OperationMode::DEEP_SLEEP;
      }
    }
    int remain = (int)((SLEEP_INTERVAL_SEC * 1000 - (millis() - waitStart)) / 1000);
    if (remain != lastRemain)
    {
      oledUpdateCountdown(remain);
      updateRelayIndicator(remain, relayMode);
      lastRemain = remain;
    }
    delay(50);
  }
}
