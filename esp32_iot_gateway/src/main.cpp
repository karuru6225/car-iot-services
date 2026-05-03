// esp32_iot_gateway
//
// 起動 → (BTN0 長押し) メニュー → LTE 接続 → OTA チェック → loop()
//
// loop() の動作モード:
//   DEEP_SLEEP  : measureAndPublish() → DeepSleep 5分（デフォルト本番動作）
//   CONTINUOUS  : measureAndPublish() → 5分待機 → 繰り返し（BTN1 長押しで DEEP_SLEEP に切り替え）
//
// デバッグモード: #define DEBUG_MODE を有効にするとデフォルトモードが CONTINUOUS になる

#define DEBUG_MODE

#include <Arduino.h>
#include "config.h"
#include "device/lte.h"
#include "service/logger.h"
#include "service/ota.h"
#include "service/mqtt.h"
#include "service/monitor.h"

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
#define UNITX_EN_PIN 9
#define CHG_ON_PIN 21

#ifdef DEBUG_MODE
static OperationMode g_mode = OperationMode::CONTINUOUS;
#else
static OperationMode g_mode = OperationMode::DEEP_SLEEP;
#endif

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
  button.begin();
  bleScanner.setup();

  // BTN0 を押しながら起動でメニューモードへ（LTE 未起動のままオフライン動作）
  delay(1300);
  if (button.isDown(0))
  {
    oledPrint("Menu Mode");
    g_mode = enterMenuMode();
  }

  lte.setup(); // LTE_EN ON → モデム初期化 → GPRS 接続 → 時刻同期

  // Jobs で次のジョブを確認。更新あれば apply() → esp_restart()（戻らない）
  // 前回 OTA の結果報告（SUCCEEDED/FAILED）も内部で行う
  ota.check();

  // MQTT 接続が確認できた場合のみ起動を確定（LTE 障害時はロールバックさせる）
  // Jobs 経由で SUCCEEDED を報告済みの場合は confirmBoot() は no-op になる
  if (mqtt.isConnected())
    ota.confirmBoot();

  logger.printf("[MAIN] 起動完了 mode=%s\n",
                g_mode == OperationMode::CONTINUOUS ? "CONTINUOUS" : "DEEP_SLEEP");

  playMelody(boot);
  pinMode(RELAY_2_PIN, OUTPUT);
  pinMode(CHG_ON_PIN, OUTPUT);
}

void loop()
{
  measureAndPublish();

  if (g_mode == OperationMode::DEEP_SLEEP)
  {
    logger.println("[MAIN] DeepSleep へ移行");
    lte.disconnect();
    lte.radioOff(); // LTE_EN LOW
    esp_sleep_enable_timer_wakeup((uint64_t)SLEEP_INTERVAL_SEC * 1000000ULL);
    esp_deep_sleep_start();
  }

  // CONTINUOUS: SLEEP_INTERVAL_SEC 秒待機しながらボタン監視・カウントダウン表示
  unsigned long waitStart = millis();
  int lastRemain = -1;
  while (millis() - waitStart < (uint32_t)SLEEP_INTERVAL_SEC * 1000)
  {
    if (button.read() == ButtonEvent::BTN1_LONG)
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
      lastRemain = remain;
    }
    delay(50);
  }
}
