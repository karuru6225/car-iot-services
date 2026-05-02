#include "ota.h"
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
    return false;
  }

  err = esp_ota_end(handle);
  if (err != ESP_OK)
  {
    logger.printf("[OTA] 検証失敗: 0x%x\n", err);
    return false;
  }

  err = esp_ota_set_boot_partition(partition);
  if (err != ESP_OK)
  {
    logger.printf("[OTA] boot partition 設定失敗: 0x%x\n", err);
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

bool Ota::updateJobStatus(const char *jobId, const char *status, const char *reason)
{
  char topic[160];
  snprintf(topic, sizeof(topic),
           "$aws/things/%s/jobs/%s/update", getDeviceId(), jobId);

  char payload[256];
  if (reason)
  {
    snprintf(payload, sizeof(payload),
             "{\"status\":\"%s\",\"statusDetails\":{\"reason\":\"%s\"}}", status, reason);
  }
  else
  {
    snprintf(payload, sizeof(payload), "{\"status\":\"%s\"}", status);
  }

  return mqtt.publish(topic, payload);
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
    updateJobStatus(jobId, "FAILED", "rollback");
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

  if (updateJobStatus(jobId, "SUCCEEDED"))
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

bool Ota::check()
{
  reportPendingJobResult();

  char acceptedTopic[128], rejectedTopic[128], getTopic[128];
  snprintf(acceptedTopic, sizeof(acceptedTopic),
           "$aws/things/%s/jobs/$next/get/accepted", getDeviceId());
  snprintf(rejectedTopic, sizeof(rejectedTopic),
           "$aws/things/%s/jobs/$next/get/rejected", getDeviceId());
  snprintf(getTopic, sizeof(getTopic),
           "$aws/things/%s/jobs/$next/get", getDeviceId());

  if (!mqtt.subscribe(acceptedTopic))
    return false;
  mqtt.subscribe(rejectedTopic);
  delay(500); // SIM7080G側のsubscription確立を待つ

  if (!mqtt.publish(getTopic, "{}"))
    return false;

  char recvTopic[128];
  static char payload[1024];
  if (!mqtt.pollMqtt(recvTopic, sizeof(recvTopic), payload, sizeof(payload), 5000))
  {
    logger.println("[OTA] Jobs レスポンスなし");
    return false;
  }

  if (strstr(recvTopic, "rejected"))
  {
    logger.println("[OTA] Jobs リクエスト拒否");
    return false;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload);
  if (err)
  {
    logger.printf("[OTA] JSON パース失敗: %s\n", err.c_str());
    return false;
  }

  JsonObject exec = doc["execution"];
  if (exec.isNull())
  {
    logger.println("[OTA] 更新ジョブなし");
    return false;
  }

  const char *jobId = exec["jobId"];
  const char *version = exec["jobDocument"]["version"];
  const char *url = exec["jobDocument"]["url"];

  if (!jobId || !url)
  {
    logger.println("[OTA] jobId または url が見つからない");
    return false;
  }

  // 現在と同じバージョンならスキップ
  if (version && strncmp(FIRMWARE_VERSION, version, strlen(version)) == 0)
  {
    logger.printf("[OTA] 同一バージョン (%s)、スキップ\n", version);
    updateJobStatus(jobId, "SUCCEEDED");
    return false;
  }

  logger.printf("[OTA] ジョブ: %s  バージョン: %s\n", jobId, version ? version : "不明");
  logger.printf("[OTA] URL (先頭80文字): %.80s\n", url);

  updateJobStatus(jobId, "IN_PROGRESS");

  // AT+SH* スタックは MQTT（AT+SM*）と独立しているため、MQTT切断のみで OK
  lte.sendCmd("AT+SMDISC", 5000);

  // url・jobId は doc のライフタイムに依存するためコピーして渡す
  static char urlBuf[768];
  strncpy(urlBuf, url, sizeof(urlBuf) - 1);
  urlBuf[sizeof(urlBuf) - 1] = '\0';

  static char jobIdBuf[64];
  strncpy(jobIdBuf, jobId, sizeof(jobIdBuf) - 1);
  jobIdBuf[sizeof(jobIdBuf) - 1] = '\0';

  return apply(urlBuf, jobIdBuf);
}
