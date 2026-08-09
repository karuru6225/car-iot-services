// esp32_iot_gateway
//
// 起動 → (BTN0 長押し) メニュー → LTE 接続 → OTA チェック → loop()
//
// loop() の動作モード:
//   DEEP_SLEEP          : measure()(非同期BLEスキャン開始+アナログ計測) + publishBattery()
//                         → enterDeepSleepMode()内でBLEスキャン完了を待って収集・publish → DeepSleep（次の5分境界まで、デフォルト本番動作）
//   CONTINUOUS          : measure() + publishBattery() → continuousLoopCore()の1秒ティックで
//                         pollBleCollect()（BLEスキャン完了を非ブロッキングで収集）+ OBD-II(CAN)ポーリングを実行 → 5分待機 →
//                         繰り返し（BTN1 長押しで DEEP_SLEEP に切り替え）
//   ONE_SHOT_CONTINUOUS : Shadow ble_mode から指定。1サイクルだけ CONTINUOUS → 自動で DEEP_SLEEP
//
// CAN(GU0)は起動直後に canInit() し、モードに関わらず常時起動しておく。
// 停止するのは enterDeepSleepMode() での canDeinit() のみ。
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
#include "device/can.h"

#include "domain/ble_targets.h"
#include "domain/telemetry.h"
#include "domain/charging.h"
#include "service/menu.h"
#include "service/pubqueue.h"
#include "service/log_storage.h"
#include "service/obdpoll.h"
#include "service/operation_mode.h"

#ifdef USE_MSGPACK
static MsgPackTelemetryEncoder g_encoder;
#else
static JsonTelemetryEncoder g_encoder;
#endif

#include <esp_sleep.h>
#include <driver/gpio.h>
#include <driver/rtc_io.h>

// ESP32-S3モジュール内蔵のBOOTボタン。DeepSleep復帰用のEXT0 wakeupソース。
// board_pins.hが管理する基板固有ピンとは異なりモジュール側で固定のため、ここで#define する
#define WAKE_PIN GPIO_NUM_0

// #define DEBUG_SKIP_NETWORK

#ifdef DEBUG_MODE
static OperationMode g_mode = OperationMode::CONTINUOUS;
#else
static OperationMode g_mode = OperationMode::DEEP_SLEEP;
#endif

static esp_sleep_wakeup_cause_t g_wakeupCause = ESP_SLEEP_WAKEUP_UNDEFINED;
static MeasureResult g_lastResult = {};
// measure()で非同期BLEスキャンを開始した後、まだcollectBle()で収集していない間true。
// #ifndef DEBUG_SKIP_NETWORK内でのみtrueになるため、DEBUG_SKIP_NETWORKビルドでは
// pollBleCollect()/enterDeepSleepMode()のBLE待機は自然にno-opになる（bleScannerが未初期化のため）
static bool g_blePending = false;
static bool g_bleUpgradedToContinuous = false;
// BTN1長押しでBLE接続中にDEEP_SLEEPへ強制した場合true。
// trueの間はloop()側のBLE自動昇格を止め、ユーザーの選択をBLE切断まで尊重する
static bool g_userForcedSleep = false;

// モード遷移を一箇所に集約する。CAN は DeepSleep 突入時のみ deinit するため、
// ここではモードの切り替えのみ行う（init/deinit には関与しない）
static void setOperationMode(OperationMode newMode)
{
  g_mode = newMode;
}

// モードごとの実行関数（setup() で modeManager に登録するため前方宣言）
static void enterDeepSleepMode();
static void runContinuousMode();
static void runOneShotContinuousMode();

void setup()
{
  g_wakeupCause = esp_sleep_get_wakeup_cause();
  gpio_hold_dis((gpio_num_t)boardPins().chgOnPin);

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

  oledInit();
  if (!adsInit())
    logger.println("[MAIN] ADS1115 初期化失敗");
  if (!ina228.init())
    logger.println("[MAIN] INA228 初期化失敗");
  if (!canInit()) // CAN通信は起動直後から常時試みる。停止するのはDeepSleep突入時のみ（enterDeepSleepMode参照）
    logger.println("[MAIN] CAN 初期化失敗");
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
    setOperationMode(enterMenuMode());
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
    setOperationMode(*override);

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
  pinMode(boardPins().relay0Pin, OUTPUT);
  pinMode(boardPins().relay1Pin, OUTPUT);
  pinMode(boardPins().relay2Pin, OUTPUT);
  pinMode(boardPins().chgOnPin, OUTPUT);
  digitalWrite(boardPins().relay0Pin, LOW);
  digitalWrite(boardPins().relay1Pin, LOW);
  digitalWrite(boardPins().relay2Pin, LOW);
  digitalWrite(boardPins().chgOnPin, isCharging() ? HIGH : LOW);

  modeManager.registerMode(OperationMode::DEEP_SLEEP, enterDeepSleepMode);
  modeManager.registerMode(OperationMode::CONTINUOUS, runContinuousMode);
  modeManager.registerMode(OperationMode::ONE_SHOT_CONTINUOUS, runOneShotContinuousMode);
}

// BLE 切断 → DEEP_SLEEP に戻す（BLE 接続で昇格した場合のみ）。
// 切断をもってg_userForcedSleepもリセットし、次回接続時は通常通り自動昇格させる
static void updateBleReconnectState()
{
  if (g_bleUpgradedToContinuous && !blePeripheral.isConnected())
  {
    setOperationMode(OperationMode::DEEP_SLEEP);
    g_bleUpgradedToContinuous = false;
  }
  if (!blePeripheral.isConnected())
    g_userForcedSleep = false;
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

// 電圧に基づく充電制御（CONTINUOUS / DEEP_SLEEP 共通）。判定ロジック自体は
// domain/charging.h の decideCharging()（ハードウェア非依存の純粋関数）に委譲する
static void updateChargingState()
{
  float vMain = g_lastResult.reading.main.voltage;
  float vSub = g_lastResult.reading.sub.voltage;
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

// measure()で開始した非同期BLEスキャンの完了を確認し、完了していれば収集してpublishする。
// スキャン中または既に収集済み（g_blePending=false）なら何もしない
static void pollBleCollect()
{
  if (!g_blePending)
    return;
  if (!collectBle(g_lastResult))
    return;
  publishBle(g_lastResult);
  g_blePending = false;
}

// shadow 同期 → LTE 切断 → DeepSleep（戻らない）
static void enterDeepSleepMode()
{
  updateChargingState();
  shadowPublishConfig();
  shadowPollDelta();
  delay(1500); // SIM7080G の TCP 送信バッファをフラッシュさせてから切断
#ifndef DEBUG_SKIP_NETWORK
  // このサイクルのBLEスキャン結果をqueue.save()に含めるため、非同期スキャンの完了を待って収集する。
  // NimBLEスタック異常時に無限待機しないようSCAN_TIME+マージンで打ち切り、強制停止する
  if (g_blePending)
  {
    unsigned long bleWaitStart = millis();
    const unsigned long bleWaitTimeoutMs = (SCAN_TIME + 3) * 1000UL;
    while (!collectBle(g_lastResult) && millis() - bleWaitStart < bleWaitTimeoutMs)
      delay(50);
    if (bleScanner.isScanning())
    {
      logger.println("[MAIN] BLEスキャン完了待ちタイムアウト → 強制停止しこの周期はBLEデータなしで続行");
      bleScanner.stop();
      collectBle(g_lastResult);
    }
    publishBle(g_lastResult);
    g_blePending = false;
  }

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

// メニューへの遷移（BTN0 短押し）。CONTINUOUS 系モード共通の処理
static void handleMenuButton()
{
  setOperationMode(enterMenuMode());
  if (g_mode == OperationMode::CONTINUOUS && blePeripheral.isConnected())
    g_bleUpgradedToContinuous = true;
}

// CONTINUOUS 系モードごとの差分。continuousLoopCore() に渡して振る舞いを変える
struct ContinuousLoopHooks
{
  void (*onTick)();       // 1秒ティックの追加処理。不要なら nullptr
  bool showCountdown;     // カウントダウン表示するか
  OperationMode selfMode; // BTN1長押しでこのモードから抜ける判定に使う
};

// このサイクル終了後に遷移する動作モードを予測する（OLED表示用）。
// 実際の遷移判定（updateBleReconnectState/runOneShotContinuousMode）と条件を揃えてある
static const char *nextModeLabel(const ContinuousLoopHooks &hooks)
{
  if (hooks.selfMode == OperationMode::ONE_SHOT_CONTINUOUS)
    return "DEEP_SLEEP"; // 1サイクルのみで必ずDEEP_SLEEPへ戻る
  if (g_userForcedSleep)
    return "DEEP_SLEEP"; // BTN1長押しでBLE接続中に強制スリープ選択済み
  if (g_bleUpgradedToContinuous && !blePeripheral.isConnected())
    return "DEEP_SLEEP"; // BLEで昇格したがBLEが切れているため次回で自動的に戻る
  return "CONTINUOUS";
}

// 次の5分境界（UTC）まで待機しながらボタン監視・カウントダウン表示・BLE Notify
static void continuousLoopCore(const ContinuousLoopHooks &hooks)
{
  unsigned long waitMs = (unsigned long)secsToNextBoundary() * 1000UL;
  unsigned long waitStart = millis();
  unsigned long lastNotify = 0;
  int lastRemain = -1;

  // g_modeがhooks.selfModeから変わった時点で即座に抜け、modeManagerへ制御を返す
  // （そのままだと最大waitMs経過するまでモード変更が実際には反映されない）
  while (millis() - waitStart < waitMs && g_mode == hooks.selfMode)
  {
    ButtonEvent ev = button.read();
    if (ev == ButtonEvent::BTN0_SHORT)
    {
      handleMenuButton();
      int curRemain = (int)((waitMs - (millis() - waitStart)) / 1000);
      oledShowSensorData(g_lastResult.reading);
      oledUpdateCountdown(curRemain, nextModeLabel(hooks));
      lastRemain = curRemain;
    }
    if (ev == ButtonEvent::BTN1_LONG)
    {
      if (g_mode == OperationMode::DEEP_SLEEP)
      {
        logger.println("[MAIN] BTN1 長押し → CONTINUOUS モードへ切り替え");
        oledPrint("Switching continuous...");
        setOperationMode(OperationMode::CONTINUOUS);
      }
      else if (g_mode == hooks.selfMode)
      {
        logger.println("[MAIN] BTN1 長押し → DEEP_SLEEP モードへ切り替え");
        oledPrint("Switching sleep...");
        setOperationMode(OperationMode::DEEP_SLEEP);
        g_bleUpgradedToContinuous = false;
        // BLE接続中の手動切り替えは、loop()側の自動昇格に即座に上書きされてしまうため
        // (BLE接続 && DEEP_SLEEP の条件が真になる)、切断されるまでは尊重する
        if (blePeripheral.isConnected())
          g_userForcedSleep = true;
      }
    }

    int remain = (int)((waitMs - (millis() - waitStart)) / 1000);
    if (remain != lastRemain)
    {
      if (hooks.showCountdown)
        oledUpdateCountdown(remain, nextModeLabel(hooks));
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

      if (hooks.onTick)
        hooks.onTick();

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

// CONTINUOUS の1秒ティック追加処理。OLED は電圧/電流画面を優先するため OBD 結果は出さず、
// ログ出力と BLE Notify にのみ使う。AWSへの送信は未実装
static void obdTick()
{
  OBDReading r = obdPoll();
  blePeripheral.notifyObd(r);
}

static void runContinuousMode() { continuousLoopCore({obdTick, true, OperationMode::CONTINUOUS}); }

static void runOneShotContinuousMode()
{
  continuousLoopCore({nullptr, true, OperationMode::ONE_SHOT_CONTINUOUS}); // 1サイクルだけ CONTINUOUS と同じ動作（BLE アドバタイズ継続）
  setOperationMode(OperationMode::DEEP_SLEEP); // 次回 loop() で DEEP_SLEEP が実行される
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
  g_blePending = true; // BLE分はpollBleCollect()/enterDeepSleepMode()側で非同期に収集する
  publishBattery(g_lastResult.reading);
  queue.flush();
  shadowPollDelta();
  oledShowSensorData(g_lastResult.reading);
#endif

  // BLE 接続 → CONTINUOUS 昇格。未接続なら DeepSleep 突入前に BLE_WAKE_WINDOW_SEC 秒だけ
  // 接続を待つ（起床直後の setup() 中もアドバタイズ済みのため、実際の待受はそれより長い）。
  // g_userForcedSleep中はBTN1でのDEEP_SLEEP選択をBLE接続中でも尊重し、自動昇格させない
  if (blePeripheral.isConnected() && g_mode == OperationMode::DEEP_SLEEP && !g_userForcedSleep)
  {
    setOperationMode(OperationMode::CONTINUOUS);
    g_bleUpgradedToContinuous = true;
  }
  else if (g_mode == OperationMode::DEEP_SLEEP && !g_userForcedSleep)
  {
    unsigned long waitStart = millis();
    while (millis() - waitStart < BLE_WAKE_WINDOW_SEC * 1000UL)
    {
      if (blePeripheral.isConnected())
      {
        setOperationMode(OperationMode::CONTINUOUS);
        g_bleUpgradedToContinuous = true;
        break;
      }
      delay(100);
    }
  }

  modeManager.run(g_mode);
}
