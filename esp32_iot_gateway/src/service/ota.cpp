#include "ota.h"
#include "jobs.h"
#include "../device/lte.h"
#include "../device/oled.h"
#include "mqtt.h"
#include "https.h"
#include "logger.h"
#include "../config.h"
#include <esp_ota_ops.h>
#include <ArduinoJson.h>

Ota ota;

bool Ota::apply(const char *url, const char *jobId)
{
  const esp_partition_t *partition = esp_ota_get_next_update_partition(NULL);
  if (!partition)
  {
    logger.println("[OTA] 更新パーティションなし");
    return false;
  }
  logger.printf("[OTA] 書き込み先: %s\n", partition->label);
  logger.printf("[OTA] DL URL: %.100s\n", url);

  oledShowOtaProgress("Downloading...", 0, 0);
  uint32_t t0 = millis();
  int fileSize = https.download(url, "firmware.bin");
  if (fileSize <= 0)
  {
    logger.println("[OTA] ダウンロード失敗");
    oledPrint("OTA DL failed");
    return false;
  }
  logger.printf("[OTA] Phase1(DL): %u ms  %d bytes\n", millis() - t0, fileSize);

  esp_ota_handle_t handle;
  esp_err_t err = esp_ota_begin(partition, OTA_SIZE_UNKNOWN, &handle);
  if (err != ESP_OK)
  {
    logger.printf("[OTA] begin 失敗: 0x%x\n", err);
    return false;
  }

  size_t written = 0;
  int lastPct = -1;
  uint32_t t1 = millis();
  bool readOk = lte.readFile("firmware.bin",
                             [&](const uint8_t *data, size_t len) -> bool
                             {
                               esp_err_t e = esp_ota_write(handle, data, len);
                               if (e != ESP_OK)
                               {
                                 logger.printf("[OTA] write 失敗: 0x%x\n", e);
                                 return false;
                               }
                               written += len;
                               int pct = (int)(written * 100 / fileSize);
                               if (pct != lastPct)
                               {
                                 oledShowOtaProgress("Writing...", written, fileSize);
                                 lastPct = pct;
                               }
                               return true;
                             });

  logger.printf("[OTA] Phase2(UART読み取り+書き込み): %u ms  %u bytes\n", millis() - t1, written);

  if (!readOk)
  {
    esp_ota_abort(handle);
    logger.println("[OTA] ファイル読み取り失敗");
    oledPrint("OTA read failed");
    return false;
  }

  err = esp_ota_end(handle);
  if (err != ESP_OK)
  {
    logger.printf("[OTA] 検証失敗: 0x%x\n", err);
    oledPrint("OTA verify failed");
    return false;
  }

  err = esp_ota_set_boot_partition(partition);
  if (err != ESP_OK)
  {
    logger.printf("[OTA] boot partition 設定失敗: 0x%x\n", err);
    oledPrint("OTA boot set fail");
    return false;
  }

  // 書き込み完了確定後に JobID を保存する（接続失敗時は保存しないためJobが FAILED にならない）
  setPendingJobId(jobId);
  lte.deleteFile("firmware.bin");

  logger.printf("[OTA] 完了 (%u bytes) → 再起動\n", written);
  delay(500);
  esp_restart();
  return true; // unreachable
}

void Ota::confirmBoot()
{
  const esp_partition_t *running = esp_ota_get_running_partition();
  esp_ota_img_states_t state;
  if (esp_ota_get_state_partition(running, &state) != ESP_OK)
    return;
  if (state != ESP_OTA_IMG_PENDING_VERIFY)
    return;
  esp_ota_mark_app_valid_cancel_rollback();
  logger.println("[OTA] 起動確認完了");
}

void Ota::reportPendingJobResult()
{
  char jobId[64] = {};
  if (!getPendingJobId(jobId, sizeof(jobId)))
    return;
  logger.printf("[OTA] 保留ジョブ確認: %s\n", jobId);

  // ロールバック検出: 次の書き込み先パーティションが ABORTED なら旧FWに戻っている
  bool rolledBack = false;
  const esp_partition_t *nextUpdate = esp_ota_get_next_update_partition(NULL);
  if (nextUpdate)
  {
    esp_ota_img_states_t nextState;
    if (esp_ota_get_state_partition(nextUpdate, &nextState) == ESP_OK)
      rolledBack = (nextState == ESP_OTA_IMG_ABORTED || nextState == ESP_OTA_IMG_INVALID);
  }

  if (rolledBack)
  {
    jobsReport(jobId, "FAILED", "rollback");
    logger.printf("[OTA] ジョブ %s: FAILED（ロールバック検出）\n", jobId);
    clearPendingJobId();
    return;
  }

  // 新ファームウェア初回起動なら先に起動確認（MQTT 失敗でもロールバックを防止）
  const esp_partition_t *running = esp_ota_get_running_partition();
  esp_ota_img_states_t runState;
  if (esp_ota_get_state_partition(running, &runState) == ESP_OK &&
      runState == ESP_OTA_IMG_PENDING_VERIFY)
    esp_ota_mark_app_valid_cancel_rollback();

  if (jobsReport(jobId, "SUCCEEDED"))
  {
    logger.printf("[OTA] ジョブ %s: SUCCEEDED\n", jobId);
    clearPendingJobId();
  }
  else
  {
    // MQTT 失敗: job_id を残して次回起動時に再試行
    logger.printf("[OTA] ジョブ %s: MQTT 失敗、次回起動時に再試行\n", jobId);
  }
}

bool Ota::handleJob(const JobInfo &job)
{
  JsonDocument doc;
  if (deserializeJson(doc, job.document) != DeserializationError::Ok)
  {
    logger.println("[OTA] ジョブドキュメントのパース失敗");
    jobsReport(job.id, "FAILED", "parse error");
    return false;
  }

  const char *version = doc["version"];
  const char *url     = doc["url"];

  if (!url)
  {
    logger.println("[OTA] url が見つからない");
    jobsReport(job.id, "FAILED", "no url");
    return false;
  }

  // 同一バージョンならスキップ
  if (version && strncmp(FIRMWARE_VERSION, version, strlen(version)) == 0)
  {
    logger.printf("[OTA] 同一バージョン (%s)、スキップ\n", version);
    jobsReport(job.id, "SUCCEEDED");
    return false;
  }

  logger.printf("[OTA] ジョブ: %s  バージョン: %s\n", job.id, version ? version : "不明");
  logger.printf("[OTA] URL (先頭80文字): %.80s\n", url);

  jobsReport(job.id, "IN_PROGRESS");
  lte.sendCmd("AT+SMDISC", 5000);

  // url は doc のライフタイムに依存するためコピーして渡す
  static char urlBuf[768];
  strncpy(urlBuf, url, sizeof(urlBuf) - 1);
  urlBuf[sizeof(urlBuf) - 1] = '\0';

  return apply(urlBuf, job.id);
}
