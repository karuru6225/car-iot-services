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
//   TIMED_CONTINUOUS    : Shadow override_next_mode="timed_continuous"+continuous_duration_min から指定。
//                         指定分数が経過するまで CONTINUOUS サイクルを繰り返し、期限到達で自動 DEEP_SLEEP
//                         （BTN1 長押しでも即座に DEEP_SLEEP に切り替え可能）
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
#include "service/mode_context.h"
#include "service/mode_common.h"

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

static esp_sleep_wakeup_cause_t g_wakeupCause = ESP_SLEEP_WAKEUP_UNDEFINED;

// 【ステージ1限定の一時アダプタ】既存のfree function(enterDeepSleepMode等)をIOperationModeHandlerで
// ラップするだけのクラス。DeepSleepModeHandler等への切り出し（ステージ3・4）が完了したら削除する
class LegacyModeHandler : public IOperationModeHandler
{
public:
  explicit LegacyModeHandler(void (*runFunc)()) : _runFunc(runFunc) {}
  void run() override { _runFunc(); }

private:
  void (*_runFunc)();
};

// モードごとの実行関数（setup() で modeManager に登録するため前方宣言）
static void enterDeepSleepMode();
static void runContinuousMode();
static void runTimedContinuousMode();

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

#ifdef DEBUG_MODE
  modeCtx.setMode(OperationMode::CONTINUOUS);
#endif

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
      uint32_t durationMin = getShadowContinuousDurationMin();
      modeCtx.setContinuousUntilEpoch(time(nullptr) + (time_t)durationMin * 60);
      logger.printf("[MAIN] TIMED_CONTINUOUS %u分間 開始\n", durationMin);
    }
  }

  oledPrint("Job checking...");
  jobsSetup();
  checkAndHandleJob();
#endif

  logger.printf("[MAIN] 起動完了 mode=%s\n",
                modeCtx.mode() == OperationMode::CONTINUOUS ? "CONTINUOUS" : "DEEP_SLEEP");

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

  static LegacyModeHandler deepSleepAdapter(enterDeepSleepMode);
  static LegacyModeHandler continuousAdapter(runContinuousMode);
  static LegacyModeHandler timedContinuousAdapter(runTimedContinuousMode);
  modeManager.registerMode(OperationMode::DEEP_SLEEP, &deepSleepAdapter);
  modeManager.registerMode(OperationMode::CONTINUOUS, &continuousAdapter);
  modeManager.registerMode(OperationMode::TIMED_CONTINUOUS, &timedContinuousAdapter);
}

// BLE 切断 → DEEP_SLEEP に戻す（BLE 接続で昇格した場合のみ）。
// 切断をもってuserForcedSleepもリセットし、次回接続時は通常通り自動昇格させる
static void updateBleReconnectState()
{
  if (modeCtx.bleUpgradedToContinuous() && !blePeripheral.isConnected())
  {
    modeCtx.setMode(OperationMode::DEEP_SLEEP);
    modeCtx.setBleUpgradedToContinuous(false);
  }
  if (!blePeripheral.isConnected())
    modeCtx.setUserForcedSleep(false);
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
  modeCtx.setMode(enterMenuMode());
  if (modeCtx.mode() == OperationMode::CONTINUOUS && blePeripheral.isConnected())
    modeCtx.setBleUpgradedToContinuous(true);
}

// CONTINUOUS 系モードごとの差分。continuousLoopCore() に渡して振る舞いを変える
struct ContinuousLoopHooks
{
  void (*onTick)();       // 1秒ティックの追加処理。不要なら nullptr
  bool showCountdown;     // カウントダウン表示するか
  OperationMode selfMode; // BTN1長押しでこのモードから抜ける判定に使う
};

// このサイクル終了後に遷移する動作モードを予測する（OLED表示用）。
// 実際の遷移判定（updateBleReconnectState/runTimedContinuousMode）と条件を揃えてある
static const char *nextModeLabel(const ContinuousLoopHooks &hooks)
{
  if (hooks.selfMode == OperationMode::TIMED_CONTINUOUS && time(nullptr) >= modeCtx.continuousUntilEpoch())
    return "DEEP_SLEEP"; // TIMED_CONTINUOUSの期限到達で次回DEEP_SLEEPへ戻る
  if (modeCtx.userForcedSleep())
    return "DEEP_SLEEP"; // BTN1長押しでBLE接続中に強制スリープ選択済み
  if (modeCtx.bleUpgradedToContinuous() && !blePeripheral.isConnected())
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

  // modeCtx.mode()がhooks.selfModeから変わった時点で即座に抜け、modeManagerへ制御を返す
  // （そのままだと最大waitMs経過するまでモード変更が実際には反映されない）
  while (millis() - waitStart < waitMs && modeCtx.mode() == hooks.selfMode)
  {
    ButtonEvent ev = button.read();
    if (ev == ButtonEvent::BTN0_SHORT)
    {
      handleMenuButton();
      int curRemain = (int)((waitMs - (millis() - waitStart)) / 1000);
      oledShowSensorData(modeCtx.lastResult().reading);
      oledUpdateCountdown(curRemain, nextModeLabel(hooks));
      lastRemain = curRemain;
    }
    if (ev == ButtonEvent::BTN1_LONG)
    {
      if (modeCtx.mode() == OperationMode::DEEP_SLEEP)
      {
        logger.println("[MAIN] BTN1 長押し → CONTINUOUS モードへ切り替え");
        oledPrint("Switching continuous...");
        modeCtx.setMode(OperationMode::CONTINUOUS);
      }
      else if (modeCtx.mode() == hooks.selfMode)
      {
        logger.println("[MAIN] BTN1 長押し → DEEP_SLEEP モードへ切り替え");
        oledPrint("Switching sleep...");
        modeCtx.setMode(OperationMode::DEEP_SLEEP);
        modeCtx.setBleUpgradedToContinuous(false);
        // BLE接続中の手動切り替えは、loop()側の自動昇格に即座に上書きされてしまうため
        // (BLE接続 && DEEP_SLEEP の条件が真になる)、切断されるまでは尊重する
        if (blePeripheral.isConnected())
          modeCtx.setUserForcedSleep(true);
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

static void runContinuousMode()
{
  continuousLoopCore({obdTick, true, OperationMode::CONTINUOUS});
#ifndef DEBUG_SKIP_NETWORK
  checkAndHandleJob(); // 5分サイクルごとにOTA/コマンドJobsを確認する
#endif
}

// setup()で設定したmodeCtx.continuousUntilEpoch()までCONTINUOUSサイクルを繰り返す。
// continuousLoopCore()はBTN1長押しでも即座にDEEP_SLEEPへ抜けられる
static void runTimedContinuousMode()
{
  continuousLoopCore({obdTick, true, OperationMode::TIMED_CONTINUOUS});
#ifndef DEBUG_SKIP_NETWORK
  checkAndHandleJob(); // 5分サイクルごとにOTA/コマンドJobsを確認する
#endif
  if (modeCtx.mode() == OperationMode::TIMED_CONTINUOUS && time(nullptr) >= modeCtx.continuousUntilEpoch())
  {
    logger.println("[MAIN] TIMED_CONTINUOUS 期限到達 → DEEP_SLEEP モードへ切り替え");
    modeCtx.setMode(OperationMode::DEEP_SLEEP); // 次回 loop() で DEEP_SLEEP が実行される
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
  modeCtx.setLastResult(measure());
  modeCtx.setBlePending(true); // BLE分はpollBleCollect()/enterDeepSleepMode()側で非同期に収集する
  publishBattery(modeCtx.lastResult().reading);
  queue.flush();
  shadowPollDelta();
  oledShowSensorData(modeCtx.lastResult().reading);
#endif

  // BLE 接続 → CONTINUOUS 昇格。未接続なら DeepSleep 突入前に BLE_WAKE_WINDOW_SEC 秒だけ
  // 接続を待つ（起床直後の setup() 中もアドバタイズ済みのため、実際の待受はそれより長い）。
  // userForcedSleep中はBTN1でのDEEP_SLEEP選択をBLE接続中でも尊重し、自動昇格させない
  if (blePeripheral.isConnected() && modeCtx.mode() == OperationMode::DEEP_SLEEP && !modeCtx.userForcedSleep())
  {
    modeCtx.setMode(OperationMode::CONTINUOUS);
    modeCtx.setBleUpgradedToContinuous(true);
  }
  else if (modeCtx.mode() == OperationMode::DEEP_SLEEP && !modeCtx.userForcedSleep())
  {
    unsigned long waitStart = millis();
    while (millis() - waitStart < BLE_WAKE_WINDOW_SEC * 1000UL)
    {
      if (blePeripheral.isConnected())
      {
        modeCtx.setMode(OperationMode::CONTINUOUS);
        modeCtx.setBleUpgradedToContinuous(true);
        break;
      }
      delay(100);
    }
  }

  modeManager.runCurrent();
}
