// esp32_iot_gateway
//
// 起動 → (BTN0 長押し) メニュー → LTE 接続 → OTA チェック → loop()
//
// loop() の動作モード:
//   DEEP_SLEEP          : measure()(非同期BLEスキャン開始+アナログ計測) + publishBattery()
//                         → DeepSleepModeHandler::run()内でBLEスキャン完了を待って収集・publish → DeepSleep（次の5分境界まで、デフォルト本番動作）
//   CONTINUOUS          : measure() + publishBattery() → ContinuousModeHandlerBase::continuousLoopCore()の1秒ティックで
//                         pollBleCollect()（BLEスキャン完了を非ブロッキングで収集）+ OBD-II(CAN)ポーリングを実行 → 5分待機 →
//                         繰り返し（BTN1 長押しで DEEP_SLEEP に切り替え）
//   TIMED_CONTINUOUS    : Shadow override_next_mode="timed_continuous"+continuous_duration_min から指定。
//                         指定分数が経過するまで CONTINUOUS サイクルを繰り返し、期限到達で自動 DEEP_SLEEP
//                         （BTN1 長押しでも即座に DEEP_SLEEP に切り替え可能）
//   LIGHT_SLEEP         : DEEP_SLEEPの短周期版（メニューから手動選択）。LTE/OLED/ADS/INA228を初期化せず
//                         CAN/BLEのみ20〜30秒間隔で確認し、検知したらCONTINUOUSへ昇格、5分境界に到達したら
//                         通常のDEEP_SLEEP起床と同じフルサイクルを行う。setup()冒頭の lightSleepShortWakeGate()
//                         （service/mode_light_sleep.cpp）がこの短周期ピークを制御する
//
// 動作モードごとの処理は src/service/mode_*.{h,cpp} のハンドラクラスに実装されている。
// main.cpp は起動シーケンスと loop() の骨格のみを持つ。
//
// CAN(GU0)とBLEはsetup()冒頭（OLED/ADS/INA228/LTEより前）で初期化する。LIGHT_SLEEPの短周期
// ピーク中はここまでしか実行されず、lightSleepShortWakeGate()が検知なしのままdeep sleepへ
// 戻すため、それ以降の初期化（OLED等）は行われない。CANの停止はDeepSleepModeHandler::run()と
// LIGHT_SLEEPの各サイクル終了時（LightSleepModeHandler::run()・lightSleepShortWakeGate()）のみ。
//
// デバッグモード: #define DEBUG_MODE を有効にするとデフォルトモードが CONTINUOUS になる

// #define DEBUG_MODE

#include <Arduino.h>
#include "config.h"
#include "board_pins.h"
#include "device/lte.h"
#include "logger.h"
#include "service/ota.h"
#include "service/jobs.h"
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
#include "device/can.h"

#include "domain/ble_targets.h"
#include "domain/telemetry.h"
#include "service/menu.h"
#include "service/pubqueue.h"
#include "service/log_storage.h"

#include "service/operation_mode.h"
#include "service/mode_context.h"
#include "service/mode_common.h"
#include "service/mode_deep_sleep.h"
#include "service/mode_continuous.h"
#include "service/mode_timed_continuous.h"
#include "service/mode_light_sleep.h"

#ifdef USE_MSGPACK
static MsgPackTelemetryEncoder g_encoder;
#else
static JsonTelemetryEncoder g_encoder;
#endif

#include <esp_sleep.h>
#include <driver/gpio.h>

static esp_sleep_wakeup_cause_t g_wakeupCause = ESP_SLEEP_WAKEUP_UNDEFINED;

void setup()
{
  g_wakeupCause = esp_sleep_get_wakeup_cause();
  gpio_hold_dis((gpio_num_t)boardPins().chgOnPin);
  // LIGHT_SLEEPの短周期ピークはOLED/ADS/INA228初期化より前に(検知なしのまま)deep sleepへ
  // 戻ることがあるため、hold解除直後にここでOUTPUT化・値の再アサートまで済ませておく
  // （末尾のrelay0/1/2Pinと違いchgOnPinはenterDeepSleepFor()でholdされるため必須）
  pinMode(boardPins().chgOnPin, OUTPUT);
  digitalWrite(boardPins().chgOnPin, isCharging() ? HIGH : LOW);

#if BOARD_VERSION == 2
  // 自己保持回路: C1の遅延時間内にHIGHを再アサートしないと電源が落ちるため最優先で行う
  gpio_hold_dis((gpio_num_t)boardPins().pwrHoldPin);
  pinMode(boardPins().pwrHoldPin, OUTPUT);
  digitalWrite(boardPins().pwrHoldPin, HIGH);
#endif

  logger.init();
  delay(1000);
  logger.printf("\n=== esp32_iot_gateway %s 起動 (wakeup=%d) ===\n",
                FIRMWARE_VERSION, (int)g_wakeupCause);

#ifdef DEBUG_MODE
  modeCtx.setMode(OperationMode::CONTINUOUS);
#endif

  // CANとBLEはOLED/ADS/INA228/LTEより前に初期化する。LIGHT_SLEEPの短周期ピーク中は
  // lightSleepShortWakeGate()が検知なしのままここでdeep sleepへ戻すため、それ以降の
  // 初期化（OLED等）を毎回スキップできる
  if (!canInit()) // CAN通信は起動直後から常時試みる。停止するのはDeepSleep突入時・LIGHT_SLEEPの各サイクル終了時のみ
    logger.println("[MAIN] CAN 初期化失敗");
#ifndef DEBUG_SKIP_NETWORK
  bleScanner.setup();
  bleTargets.load();
  blePeripheral.setup();
  blePeripheral.startAdvertising();
#endif

  lightSleepShortWakeGate(); // LIGHT_SLEEPの短周期ピーク中はここで検知なしのままdeep sleepへ戻り、戻らないことがある

  oledInit();
  if (!adsInit())
    logger.println("[MAIN] ADS1115 初期化失敗");
  if (!ina228.init())
    logger.println("[MAIN] INA228 初期化失敗");
  oledPrint("FW: " FIRMWARE_VERSION);
  if (g_wakeupCause != ESP_SLEEP_WAKEUP_TIMER)
  {
    speakerInit();
    playMelody(bootStart);
  }
  button.begin();

  // BTN0 を押しながら起動でメニューモードへ（LTE 未起動のままオフライン動作）
  delay(1300);
  if (button.isDown(0))
  {
    oledPrint("Menu Mode");
    modeCtx.setMode(enterMenuMode());
    if (modeCtx.mode() == OperationMode::CONTINUOUS && blePeripheral.isConnected())
      modeCtx.setBleUpgradedToContinuous(true);
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
  {
    modeCtx.setMode(*override);
    if (*override == OperationMode::TIMED_CONTINUOUS)
    {
      if (auto untilTime = getShadowContinuousUntilTime())
      {
        modeCtx.setContinuousUntilEpoch(*untilTime);
        logger.printf("[MAIN] TIMED_CONTINUOUS %ld まで開始\n", (long)*untilTime);
      }
      else
      {
        // continuous_until_time未指定時のフォールバック（30分後）
        modeCtx.setContinuousUntilEpoch(time(nullptr) + 30 * 60);
        logger.println("[MAIN] TIMED_CONTINUOUS continuous_until_time未指定 → デフォルト30分間開始");
      }
    }
  }

  oledPrint("Job checking...");
  jobsSetup();
  checkAndHandleJob();
#endif

  logger.printf("[MAIN] 起動完了 mode=%s\n", operationModeName(modeCtx.mode()));

  if (g_wakeupCause != ESP_SLEEP_WAKEUP_TIMER)
    playMelody(boot);
  pinMode(boardPins().relay0Pin, OUTPUT);
  pinMode(boardPins().relay1Pin, OUTPUT);
  pinMode(boardPins().relay2Pin, OUTPUT);
  digitalWrite(boardPins().relay0Pin, LOW);
  digitalWrite(boardPins().relay1Pin, LOW);
  digitalWrite(boardPins().relay2Pin, LOW);
  // chgOnPinのOUTPUT化・値の再アサートはsetup()冒頭（gpio_hold_dis直後）で済ませてある

  modeManager.registerMode(OperationMode::DEEP_SLEEP, &deepSleepMode);
  modeManager.registerMode(OperationMode::CONTINUOUS, &continuousMode);
  modeManager.registerMode(OperationMode::TIMED_CONTINUOUS, &timedContinuousMode);
  modeManager.registerMode(OperationMode::LIGHT_SLEEP, &lightSleepMode);
}

void loop()
{
#ifndef DEBUG_SKIP_NETWORK
  if (!lte.isConnected())
  {
    logger.println("[MAIN] LTE 切断検出 → 再接続中...");
    oledPrint("LTE reconnecting...");
    lte.connect();
  }
  modeCtx.setLastResult(measure());
  modeCtx.setBlePending(true); // BLE分はpollBleCollect()/DeepSleepModeHandler::run()側で非同期に収集する
  publishBattery(modeCtx.lastResult().reading);
  queue.flush();
  // flush()はRTCメモリのみ操作しSPIFFSには触れない。ここでも同期しておかないと、
  // 圏内復帰でflush()がRTCキューを空にした直後にCONTINUOUSへ昇格した場合、
  // DeepSleepModeHandler::run()のsave()が呼ばれずSPIFFS上に送信済みデータが
  // 残り続け、次のリセットでload()が古いデータを重複送信してしまう。
  queue.save();
  shadowPollDelta();
  oledShowSensorData(modeCtx.lastResult().reading);
#endif

  modeManager.runCurrent();
}
