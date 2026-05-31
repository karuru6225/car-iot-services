// esp32_iot_gateway
//
// 起動 → (BTN0 長押し) メニュー → LTE 接続 → OTA チェック → loop()
//
// loop() の動作モード:
//   DEEP_SLEEP          : measure() + publish() → DeepSleep（次の5分境界まで、デフォルト本番動作）
//   CONTINUOUS          : measure() + publish() → 5分待機 → 繰り返し（BTN1 長押しで DEEP_SLEEP に切り替え）
//   ONE_SHOT_CONTINUOUS : Shadow ble_mode から指定。1サイクルだけ CONTINUOUS → 自動で DEEP_SLEEP
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
#include "device/ble_peripheral.h"
#include "device/button.h"

#include "domain/ble_targets.h"
#include "domain/telemetry.h"
#include "service/menu.h"
#include "service/pubqueue.h"
#include "service/log_storage.h"

#ifdef USE_MSGPACK
static MsgPackTelemetryEncoder g_encoder;
#else
static JsonTelemetryEncoder g_encoder;
#endif

#include <esp_sleep.h>
#include <driver/gpio.h>
#include <driver/rtc_io.h>

#define RELAY_0_PIN 11
#define RELAY_1_PIN 13
#define RELAY_2_PIN 15

#define GU0_0 4
#define GU0_1 5
#define GU0_EN 6

// #define DEBUG_SKIP_NETWORK

#ifdef DEBUG_MODE
static OperationMode g_mode = OperationMode::CONTINUOUS;
#else
static OperationMode g_mode = OperationMode::DEEP_SLEEP;
#endif

static esp_sleep_wakeup_cause_t g_wakeupCause = ESP_SLEEP_WAKEUP_UNDEFINED;
static MeasureResult g_lastResult = {};
static bool g_bleUpgradedToContinuous = false;

void setup()
{
  g_wakeupCause = esp_sleep_get_wakeup_cause();
  gpio_hold_dis((gpio_num_t)CHG_ON_PIN);

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
  blePeripheral.setup();
  blePeripheral.startAdvertising();
#endif

  // BTN0 を押しながら起動でメニューモードへ（LTE 未起動のままオフライン動作）
  delay(1300);
  if (button.isDown(0))
  {
    oledPrint("Menu Mode");
    g_mode = enterMenuMode();
    if (g_mode == OperationMode::CONTINUOUS && blePeripheral.isConnected())
      g_bleUpgradedToContinuous = true;
  }

#ifndef DEBUG_SKIP_NETWORK
  oledPrint("LTE connecting...");
  lte.setup();      // LTE_EN ON → モデム初期化 → GPRS 接続 → 時刻同期
  logStorageInit(); // 時刻同期後に呼ぶ（ファイル名に UNIX 時間を使用）

  queue.setEncoder(&g_encoder);
  queue.load();  // 電源投入時: SPIFFS → RTC メモリ（DeepSleep 復帰時は no-op）
  queue.flush(); // 前回バッファ分を即送信
  delay(500);    // SIM7080G の送信バッファ安定待ち

  ota.reportPendingJobResult();

  // MQTT 接続が確認できた場合のみ起動を確定（LTE 障害時はロールバックさせる）
  if (mqtt.isConnected())
    ota.confirmBoot();

  shadowSetup();
  shadowPublishConfig(); // reported を送信して AWS に delta を再計算させる
  shadowPollDelta(3000); // 起動時に pending な desired を即適用

  // Shadow override_next_mode を確認
  if (auto override = getShadowOverrideMode())
    g_mode = *override;

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
  digitalWrite(RELAY_0_PIN, LOW);
  digitalWrite(RELAY_1_PIN, LOW);
  digitalWrite(RELAY_2_PIN, LOW);
  digitalWrite(CHG_ON_PIN, isCharging() ? HIGH : LOW);
}


// BLE 切断 → DEEP_SLEEP に戻す（BLE 接続で昇格した場合のみ）
static void updateBleReconnectState()
{
  if (g_bleUpgradedToContinuous && !blePeripheral.isConnected())
  {
    g_mode = OperationMode::DEEP_SLEEP;
    g_bleUpgradedToContinuous = false;
  }
}

// 次の5分境界（UTC）までの秒数を返す。時刻未同期なら SLEEP_INTERVAL_SEC を返す
static uint32_t secsToNextBoundary()
{
  time_t now = time(nullptr);
  if (now <= 1577836800L) // 2020-01-01以前なら時刻未同期
    return SLEEP_INTERVAL_SEC;
  time_t next = ((now / (time_t)SLEEP_INTERVAL_SEC) + 1) * (time_t)SLEEP_INTERVAL_SEC;
  return (uint32_t)(next - now);
}

// 電圧に基づく充電制御（CONTINUOUS / DEEP_SLEEP 共通）
static void updateChargingState()
{
  float v      = g_lastResult.reading.main.voltage;
  float startV = getChgStartV();
  float stopV  = getChgStopV();
  if (v >= 10.0f && !isCharging() && v < startV)
  {
    setCharging(true);
    digitalWrite(CHG_ON_PIN, HIGH);
    logger.printf("[MAIN] auto charge ON  vMain=%.2fV < startV=%.2fV\n", v, startV);
  }
  else if (v >= 10.0f && isCharging() && v >= stopV)
  {
    setCharging(false);
    digitalWrite(CHG_ON_PIN, LOW);
    logger.printf("[MAIN] auto charge OFF vMain=%.2fV >= stopV=%.2fV\n", v, stopV);
  }
}

// shadow 同期 → LTE 切断 → DeepSleep（戻らない）
static void enterDeepSleepMode()
{
  updateChargingState();
  shadowPublishConfig();
  shadowPollDelta();
  delay(1500); // SIM7080G の TCP 送信バッファをフラッシュさせてから切断
#ifndef DEBUG_SKIP_NETWORK
  queue.save();
  lte.disconnect();
  lte.radioOff();
#endif
  oledClear();

  uint32_t sleepSec = secsToNextBoundary();
  if (isCharging())
    gpio_hold_en((gpio_num_t)CHG_ON_PIN);
  rtc_gpio_init(GPIO_NUM_0);
  rtc_gpio_set_direction(GPIO_NUM_0, RTC_GPIO_MODE_INPUT_ONLY);
  rtc_gpio_pullup_en(GPIO_NUM_0);
  rtc_gpio_pulldown_dis(GPIO_NUM_0);
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_0, 0);
  esp_sleep_enable_timer_wakeup((uint64_t)sleepSec * 1000000ULL);
  esp_deep_sleep_start();
}

// 次の5分境界（UTC）まで待機しながらボタン監視・カウントダウン表示・BLE Notify
static void runContinuousLoop()
{
  unsigned long waitMs     = (unsigned long)secsToNextBoundary() * 1000UL;
  unsigned long waitStart  = millis();
  unsigned long lastNotify = 0;
  int lastRemain           = -1;

  while (millis() - waitStart < waitMs)
  {
    ButtonEvent ev = button.read();
    if (ev == ButtonEvent::BTN0_SHORT)
    {
      g_mode = enterMenuMode();
      if (g_mode == OperationMode::CONTINUOUS && blePeripheral.isConnected())
        g_bleUpgradedToContinuous = true;
      int curRemain = (int)((waitMs - (millis() - waitStart)) / 1000);
      oledShowSensorData(g_lastResult.reading);
      oledUpdateCountdown(curRemain);
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

    int remain = (int)((waitMs - (millis() - waitStart)) / 1000);
    if (remain != lastRemain)
    {
      oledUpdateCountdown(remain);
      lastRemain = remain;
    }

    unsigned long now = millis();
    if (now - lastNotify >= 1000)
    {
      lastNotify = now;
      updateChargingState();
      blePeripheral.notify(
        adsReadDiffMain(),
        ina228.readCurrent(),
        ina228.readPower(),
        adsReadDiffSub()
      );
    }

    delay(50);
  }
}

void loop()
{
  updateBleReconnectState();

#ifndef DEBUG_SKIP_NETWORK
  if (!lte.isConnected())
  {
    logger.println("[MAIN] LTE 切断検出 → 再接続中...");
    oledPrint("LTE reconnecting...");
    lte.connect();
  }
  g_lastResult = measure();
  publish(g_lastResult);
  queue.flush();
  shadowPollDelta();
  oledShowSensorData(g_lastResult.reading);
#endif

  // BLE 接続 → CONTINUOUS 昇格
  if (blePeripheral.isConnected() && g_mode == OperationMode::DEEP_SLEEP)
  {
    g_mode = OperationMode::CONTINUOUS;
    g_bleUpgradedToContinuous = true;
  }

  if (g_mode == OperationMode::DEEP_SLEEP)
    enterDeepSleepMode(); // 戻らない
  else if (g_mode == OperationMode::ONE_SHOT_CONTINUOUS)
  {
    runContinuousLoop();                   // 1サイクル CONTINUOUS（BLE アドバタイズ継続）
    g_mode = OperationMode::DEEP_SLEEP;   // 次ループで DeepSleep へ
  }
  else
    runContinuousLoop();
}
