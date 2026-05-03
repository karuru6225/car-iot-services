// esp32_iot_gateway
//
// 起動 → LTE 接続 → OTA チェック（更新あれば適用・再起動）
// → DeepSleep（本番モード）または待機ループ（デバッグモード）
//
// デバッグモード: #define DEBUG_MODE を有効にすると DeepSleep しない
// メニューモード: 起動時に BTN0 を押しながら電源 ON で設定メニューへ

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
#include "device/ble_scan.h"
#include "device/button.h"

#include "domain/ble_targets.h"
#include "service/menu.h"

#include <esp_sleep.h>

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
  oledPrint("FW: " FIRMWARE_VERSION);
  speakerInit();
  playMelody(bootStart);
  button.begin(BTN0_PIN, BTN1_PIN);

  // BTN0 を押しながら起動でメニューモードへ（LTE 未起動のままオフライン動作）
  delay(1300);
  if (digitalRead(BTN0_PIN) == LOW)
  {
    oledPrint("Menu Mode");
    enterMenuMode(); // 戻らない
  }

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

  playMelody(boot);

  pinMode(RELAY_2_PIN, OUTPUT);

  // pinMode(UNITX_EN_PIN, OUTPUT);
  pinMode(CHG_ON_PIN, OUTPUT);
}

void loop()
{
}
