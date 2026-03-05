/*
 * M5Stamp Lite 統合スケッチ
 * 機能:
 *   1. ADS1115 電圧測定（差動 AIN1-AIN0 / 分圧回路）
 *   2. SwitchBot 防水温湿度計 BLE スキャン
 *   3. SORACOM Harvest Data (LTE/SIM7080G) への HTTP POST
 *
 * ハードウェア:
 *   - M5Atom S3 (ESP32-S3)
 *   - ADS1115 on Port A (SDA=G38, SCL=G39, ADDR=0x49)
 *   - M5Stack U128 (SIM7080G): RX=G1(GPIO1), TX=G2(GPIO2)  ※ラベル逆注意
 *
 * LTE 注意事項:
 *   - APN は CFUN=0 → CGDCONT → CFUN=1 の順で設定（必須）
 *   - modem.restart() は使わず modem.init() を使う
 *   - SORACOM SIM グループで Harvest Data を有効化すること
 */

// デバッグモード: 有効時 → 待機ループ＋登録モード、無効時 → DeepSleep
// #define DEBUG_MODE

// M5Unified は BLE より先に include（必須 - ARCHITECTURE.md 参照）
#include "view.h"
#include "config.h"
#include "certs.h"
#include "domain__targets.h"
#include "infra__ble_scan.h"
#include "infra__button.h"
#include "register_mode.h"

#include <Wire.h>
#include <Adafruit_ADS1X15.h>
#include "infra__lte.h"
#include "infra__logger.h"
#include "bypass_mode.h"

// ─── ADS1115 ─────────────────────────────────────────────────────────────
// 分圧回路（差動）: +/− → R1(680kΩ) → AIN0/AIN1 → R2(11kΩ) → GND
// GAIN_EIGHT(±0.512V) で最大 ±32V 測定可能
static const uint8_t SDA_PIN = 38;
static const uint8_t SCL_PIN = 39;
static const float R1 = 680000.0f;
static const float R2 = 11000.0f;
static const float DIV_RATIO = R2 / (R1 + R2);

Adafruit_ADS1115 ads;

// ─── モード管理 ───────────────────────────────────────────────────────────
enum Mode
{
  MODE_SCAN,
  MODE_REGISTER
};
static Mode gMode = MODE_SCAN;

// ─── ユーティリティ ───────────────────────────────────────────────────────

// ISO8601 UTC タイムスタンプを生成する（RTC 未同期なら空文字列）
static void buildTimestamp(char *buf, size_t size)
{
  time_t now = time(nullptr);
  if (now < 1000000000L)
  {
    buf[0] = '\0';
    return;
  }
  strftime(buf, size, "%Y-%m-%dT%H:%M:%SZ", gmtime(&now));
}

// ADS1115 から電圧を読み取る（分圧回路換算）
static float readVoltage()
{
  int16_t raw = ads.readADC_Differential_0_1();
  float adcV = ads.computeVolts(raw);
  return -adcV / DIV_RATIO;
}

// ─── 初期化関数 ───────────────────────────────────────────────────────────

static void initAds()
{
  Wire.begin(SDA_PIN, SCL_PIN);
  ads.setGain(GAIN_EIGHT);
  if (!ads.begin(0x49))
  {
    view.message("[ADS] ADS1115 not found!");
    while (1)
      delay(1000);
  }
  view.message("[ADS] OK");
}

static void initButton()
{
  pinMode(BTN_PIN, INPUT_PULLUP);
  button.setup();
}

static void initBle()
{
  targets.load();
  scanner.setup();
  view.message("[BLE] OK  [長押し: 登録モード]");
}

// ─── setup / loop ─────────────────────────────────────────────────────────

void setup()
{
  M5.begin();
  logger.init();
  view.clear();
  view.message("=== セットアップ中 ===");
  delay(1000);
  view.clear();
  initButton();

  // bypassMode.run();

  initAds();
  initBle();
  lte.setup();

  view.message("=== セットアップ完了 ===");
}

time_t prevLoopTime = 0;

void loop()
{
#ifdef DEBUG_MODE
  // バイパスモード: スキャン・MQTT 送信・登録モード遷移を行わずシリアル転送のみ行う
  if (Serial.available())
  {
    bypassMode.run();
    button.check(); // バイパス中に蓄積したボタンイベントを破棄
    return;
  }
#endif

  // 登録モード（前ループで長押し検出済みの場合）
  if (button.check() == BTN_LONG)
  {
    regMode.run();
    return;
  }

  if (time(nullptr) < prevLoopTime + SEND_INTERVAL_SEC * 1000UL)
    return;

  prevLoopTime = time(nullptr);

  // BLE スキャン（スキャン中も ISR がボタンイベントを記録する）
  logger.printf("\n--- BLE スキャン (%d秒) ---\n", SCAN_TIME);
  scanner.start(SCAN_TIME);
  scanner.clearResults();

  // GPRS 接続確認・再接続
  if (!lte.isConnected())
  {
    logger.println("[LTE] 切断検出 → 再接続...");
    lte.disconnect();
    delay(1000);
    if (lte.connect())
      lte.syncTime();
  }

  // タイムスタンプを1回取得して全 payload で共用
  char ts[25];
  buildTimestamp(ts, sizeof(ts));
  char tsField[32] = "";
  if (ts[0])
    snprintf(tsField, sizeof(tsField), ",\"ts\":\"%s\"", ts);

  SwitchBotData d;

#ifdef DEBUG_MODE
  // ─── デバッグモード: 送信 → 待機 ─────────────────────────────────────
  {
    float voltage = readVoltage();
    logger.printf("[ADS] 電圧: %.2f V\n", voltage);
    char payload[96];
    snprintf(payload, sizeof(payload),
             "{\"type\":\"battery\",\"id\":\"voltage_%d\",\"voltage\":%.2f%s}", 1, voltage, tsField);
    lte.publish(MQTT_TOPIC_DATA, payload);
  }
  while (xQueueReceive(scanner.queue, &d, 0) == pdTRUE)
  {
    if (!d.parsed)
      continue;
    char payload[224];
    snprintf(payload, sizeof(payload),
             "{\"type\":\"thermometer\",\"addr\":\"%s\",\"temp\":%.1f,\"humidity\":%d,\"battery\":%d,\"rssi\":%d%s}",
             d.address, d.temp, d.humidity, d.battery, d.rssi, tsField);
    lte.publish(MQTT_TOPIC_DATA, payload);
  }
  // 待機中も長押しを監視
  logger.printf("[WAIT] %d秒待機... [長押し: 登録モード]\n", SEND_INTERVAL_SEC);

#else
  // ─── 本番モード: ペイロード収集 → 3分間リトライ → DeepSleep ──────────
  // BLE キューはここで消費するため、先にすべてのペイロードを収集する
  static char payloads[MAX_TARGETS + 1][224];
  int count = 0;
  {
    float voltage = readVoltage();
    logger.printf("[ADS] 電圧: %.2f V\n", voltage);
    snprintf(payloads[count++], 224,
             "{\"type\":\"battery\",\"id\":\"voltage_%d\",\"voltage\":%.2f%s}", 1, voltage, tsField);
  }
  while (xQueueReceive(scanner.queue, &d, 0) == pdTRUE)
  {
    if (!d.parsed)
      continue;
    snprintf(payloads[count++], 224,
             "{\"type\":\"thermometer\",\"addr\":\"%s\",\"temp\":%.1f,\"humidity\":%d,\"battery\":%d,\"rssi\":%d%s}",
             d.address, d.temp, d.humidity, d.battery, d.rssi, tsField);
  }

  // 3分間リトライ（成功または期限切れで抜ける）
  unsigned long deadline = millis() + 3UL * 60 * 1000;
  bool allOk = false;
  while (true)
  {
    if (!lte.isConnected())
    {
      lte.disconnect();
      delay(1000);
      if (lte.connect())
        lte.syncTime();
    }
    allOk = true;
    for (int i = 0; i < count; i++)
      if (!lte.publish(MQTT_TOPIC_DATA, payloads[i]))
        allOk = false;
    if (allOk || millis() >= deadline)
      break;
    logger.println("[RETRY] 送信エラー → 再試行...");
    delay(5000);
  }

  logger.println(allOk ? "[SLEEP] 5分 DeepSleep" : "[SLEEP] タイムアウト → 5分 DeepSleep");

  if (button.check() == BTN_LONG)
  {
    regMode.run();
  }
  // DeepSleep前にMQTT/GPRS切断（モジュールのSMSTATEをリセットする）
  // これがないと復帰後にSMSTATE=1の誤認のままpublishしてデータが届かない
  delay(2000); // publish後のパケット送信完了を待つ
  lte.disconnect();
  lte.radioOff(); // ラジオオフでスリープ中の電力消費を抑える

  // 消費電力低減: UART / I2C を終了しピンをハイインピーダンスに設定
  // gpio_config でプルアップ/プルダウンも明示的に無効化する
  SerialAT.end();
  Wire.end();
  {
    gpio_config_t io_conf = {};
    io_conf.pin_bit_mask   = (1ULL << LTE_TX_PIN) | (1ULL << LTE_RX_PIN)
                           | (1ULL << SDA_PIN)     | (1ULL << SCL_PIN);
    io_conf.mode           = GPIO_MODE_INPUT;
    io_conf.pull_up_en     = GPIO_PULLUP_DISABLE;
    io_conf.pull_down_en   = GPIO_PULLDOWN_DISABLE;
    io_conf.intr_type      = GPIO_INTR_DISABLE;
    gpio_config(&io_conf);
  }

  esp_sleep_enable_timer_wakeup((uint64_t)SLEEP_INTERVAL_SEC * 1000000ULL);
  esp_deep_sleep_start();
#endif
}
