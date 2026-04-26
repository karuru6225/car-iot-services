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
#include "infra/lte.h"
#include "infra/logger.h"
#include "infra/ota.h"

#include <esp_sleep.h>

void setup()
{
  logger.init();
  delay(500);
  logger.printf("\n=== esp32_iot_gateway %s 起動 ===\n", FIRMWARE_VERSION);

  lte.setup(); // LTE_EN ON → モデム初期化 → GPRS 接続 → 時刻同期

  // Jobs で次のジョブを確認。更新あれば apply() → esp_restart()（戻らない）
  // 前回 OTA の結果報告（SUCCEEDED/FAILED）も内部で行う
  ota.check();

  // MQTT 接続が確認できた場合のみ起動を確定（LTE 障害時はロールバックさせる）
  // Jobs 経由で SUCCEEDED を報告済みの場合は confirmBoot() は no-op になる
  if (lte.isMqttConnected())
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
}

void loop()
{
#ifdef DEBUG_MODE
  delay(30000);
  logger.println("[LOOP] 30秒経過（OTA 待ち受け中）");

  // デバッグモードでも定期的に OTA チェック
  if (lte.isConnected())
    ota.check();
#endif
}
