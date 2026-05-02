// esp32_iot_gateway
// フェーズ1: OTA ファームウェアアップデート
//
// 起動 → LTE 接続 → OTA チェック（更新あれば適用・再起動）
// → DeepSleep（本番モード）または待機ループ（デバッグモード）
//
// デバッグモード: #define DEBUG_MODE を有効にすると DeepSleep しない

#define DEBUG_MODE

#include <Arduino.h>
#include "config.h"
#include "device/lte.h"
#include "service/logger.h"
#include "service/ota.h"
#include "service/mqtt.h"

#include "device/speaker.h"
#include "device/oled.h"
#include "device/ads.h"
#include "device/ina228.h"

#include "domain/measurement.h"
#include "domain/telemetry.h"

#include <esp_sleep.h>

#define SPEEKER_PIN 34
#define RELAY_2_PIN 15
#define BTN0_PIN 26
#define BTN1_PIN 33
#define UNITX_EN_PIN 9
#define CHG_ON_PIN 21

void setup()
{
  logger.init();
  delay(1000);
  logger.printf("\n=== esp32_iot_gateway %s 起動 ===\n", FIRMWARE_VERSION);

  oledInit();
  adsInit();
  ina228Init();
  oledPrint("Hello, world!");

  lte.setup(); // LTE_EN ON → モデム初期化 → GPRS 接続 → 時刻同期

  // Jobs で次のジョブを確認。更新あれば apply() → esp_restart()（戻らない）
  // 前回 OTA の結果報告（SUCCEEDED/FAILED）も内部で行う
  ota.check();

  // MQTT 接続が確認できた場合のみ起動を確定（LTE 障害時はロールバックさせる）
  // Jobs 経由で SUCCEEDED を報告済みの場合は confirmBoot() は no-op になる
  if (mqtt.isConnected())
    ota.confirmBoot();

#ifndef DEBUG_MODE
  logger.println("[MAIN] OTA チェック完了 → DeepSleep");
  lte.disconnect();
  lte.radioOff(); // LTE_EN LOW
  esp_sleep_enable_timer_wakeup((uint64_t)SLEEP_INTERVAL_SEC * 1000000ULL);
  esp_deep_sleep_start();
#else
  logger.println("[MAIN] OTA チェック完了（デバッグモード: 待機ループ）");
#endif
  // tone(SPEEKER_PIN, 1000, 100); // 起動確認用の音
  // delay(100);
  // tone(SPEEKER_PIN, 1500, 100);
  // delay(100);
  // tone(SPEEKER_PIN, 2000, 100);

  playMelody(SPEEKER_PIN);
  pinMode(SPEEKER_PIN, OUTPUT);
  digitalWrite(SPEEKER_PIN, HIGH); // SPEAKER_PINは負論理。HIGHで回路遮断

  pinMode(RELAY_2_PIN, OUTPUT);
  pinMode(BTN0_PIN, INPUT_PULLUP);
  pinMode(BTN1_PIN, INPUT_PULLUP);

  // pinMode(UNITX_EN_PIN, OUTPUT);
  pinMode(CHG_ON_PIN, OUTPUT);

  VoltageReading v1 = {adsReadDiff01()};
  VoltageReading v2 = {adsReadDiff23()};
  PowerReading pwr  = {ina228ReadCurrent(), ina228ReadPower(), ina228ReadTemp()};

  char topic[80];
  snprintf(topic, sizeof(topic), "$aws/things/%s/shadow/update", getDeviceId());
  char payload[256];
  buildShadowPayload(payload, sizeof(payload), v1, v2, pwr, time(nullptr));
  mqtt.publish(topic, payload);
  delay(1000);
  lte.powerOff(); // 電源オフ（完全に電源を切る。再度電源オンするには setup() を呼ぶ必要がある）
}

void loop()
{
  bool btn0 = digitalRead(BTN0_PIN) == LOW;
  bool btn1 = digitalRead(BTN1_PIN) == LOW;
  float voltage = adsReadDiff01();
  float voltage2 = adsReadDiff23();

  digitalWrite(RELAY_2_PIN, HIGH);
  // digitalWrite(UNITX_EN_PIN, HIGH);
  digitalWrite(CHG_ON_PIN, HIGH);
  oledShowStatus(voltage, voltage2, true, btn0, btn1);
  delay(3000);
  digitalWrite(RELAY_2_PIN, LOW);
  // digitalWrite(UNITX_EN_PIN, LOW);
  digitalWrite(CHG_ON_PIN, LOW);
  oledShowStatus(voltage, voltage2, false, btn0, btn1);
  delay(3000);
  ina228PrintStatus();
}
