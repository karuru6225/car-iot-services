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
#include <cstdarg>

extern "C"
{
#include "../../lib/uzlib/uzlib.h"
}

Ota ota;

// ─── gz OTA ストリーミング解凍 ───────────────────────────────────────────────

static void gzLog(const char *fmt, ...)
{
  char buf[128];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  logger.print(buf);
}

struct GzStream
{
  const char *filename;
  int fileSize;
  int filePos;
  static const int BUF_SIZE = 4096;
  uint8_t buf[BUF_SIZE];
  int bufLen;
  int bufPos;
};
static GzStream s_gz;

static unsigned int gzReadByte(TINF_DATA *d, unsigned char *out)
{
  (void)d;
  if (s_gz.bufPos >= s_gz.bufLen)
  {
    if (s_gz.filePos >= s_gz.fileSize)
      return (unsigned int)-1;
    int toRead = min(GzStream::BUF_SIZE, s_gz.fileSize - s_gz.filePos);
    int got = lte.fileReadChunk(s_gz.filename, s_gz.filePos, s_gz.buf, toRead);
    if (got <= 0)
      return (unsigned int)-1;
    s_gz.bufLen = got;
    s_gz.bufPos = 0;
    s_gz.filePos += got;
  }
  *out = s_gz.buf[s_gz.bufPos++];
  return 0;
}

// ─── OTA 書き込みヘルパー ────────────────────────────────────────────────────

static bool writeBinToOta(const char *filename, int fileSize,
                          esp_ota_handle_t handle, size_t &written)
{
  written = 0;
  int lastPct = -1;
  return lte.readFile(filename, [&](const uint8_t *data, size_t len) -> bool
                      {
    if (esp_ota_write(handle, data, len) != ESP_OK)
    {
      logger.println("[OTA] write 失敗");
      return false;
    }
    written += len;
    int pct = (int)(written * 100 / fileSize);
    if (pct != lastPct)
    {
      oledShowOtaProgress("Writing...", written, fileSize);
      lastPct = pct;
    }
    return true; });
}

static bool writeGzToOta(const char *filename, esp_ota_handle_t handle, size_t &written)
{
  int fileSize = lte.fileOpen(filename);
  if (fileSize < 0)
  {
    logger.println("[OTA] gz fileOpen 失敗");
    return false;
  }

  s_gz.filename = filename;
  s_gz.fileSize = fileSize;
  s_gz.filePos = 0;
  s_gz.bufLen = 0;
  s_gz.bufPos = 0;

  static uint8_t dict[32768];
  TINF_DATA d = {};
  d.source = NULL;
  d.readSourceByte = gzReadByte;
  d.log = gzLog;
  uzlib_uncompress_init(&d, dict, sizeof(dict));

  if (uzlib_gzip_parse_header(&d) != TINF_OK)
  {
    logger.println("[OTA] gz ヘッダパース失敗");
    lte.fileClose();
    return false;
  }

  static uint8_t outBuf[4096];
  written = 0;
  d.destSize = sizeof(outBuf);
  int ret = TINF_OK;

  while (ret == TINF_OK)
  {
    d.dest = outBuf;
    ret = uzlib_uncompress_chksum(&d);

    size_t produced = (size_t)(d.dest - outBuf);
    if (produced > 0)
    {
      if (esp_ota_write(handle, outBuf, produced) != ESP_OK)
      {
        logger.println("[OTA] gz write 失敗");
        lte.fileClose();
        return false;
      }
      written += produced;
      oledShowOtaProgress("Writing...", s_gz.filePos, fileSize);
    }
  }

  lte.fileClose();

  if (ret != TINF_DONE)
  {
    logger.printf("[OTA] gz 解凍エラー: %d\n", ret);
    return false;
  }

  logger.printf("[OTA] gz 解凍完了: %u bytes\n", (unsigned)written);
  return true;
}

// ─── apply ──────────────────────────────────────────────────────────────────

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

  // .gz か .bin かを URL 末尾で判定
  size_t urlLen = strlen(url);
  bool isGz = urlLen >= 3 && strcmp(url + urlLen - 3, ".gz") == 0;
  const char *tmpFile = isGz ? "firmware.gz" : "firmware.bin";

  oledShowOtaProgress("Downloading...", 0, 0);
  uint32_t t0 = millis();
  int dlSize = https.download(url, tmpFile);
  if (dlSize <= 0)
  {
    logger.println("[OTA] ダウンロード失敗");
    oledPrint("OTA DL failed");
    return false;
  }
  logger.printf("[OTA] Phase1(DL): %u ms  %d bytes\n", millis() - t0, dlSize);

  esp_ota_handle_t handle;
  esp_err_t err = esp_ota_begin(partition, OTA_SIZE_UNKNOWN, &handle);
  if (err != ESP_OK)
  {
    logger.printf("[OTA] begin 失敗: 0x%x\n", err);
    return false;
  }

  size_t written = 0;
  uint32_t t1 = millis();
  bool writeOk;

  if (isGz)
    writeOk = writeGzToOta(tmpFile, handle, written);
  else
    writeOk = writeBinToOta(tmpFile, dlSize, handle, written);

  logger.printf("[OTA] Phase2(書き込み): %u ms  %u bytes\n", millis() - t1, (unsigned)written);

  if (!writeOk)
  {
    esp_ota_abort(handle);
    oledPrint("OTA write failed");
    lte.deleteFile(tmpFile);
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
  lte.deleteFile(tmpFile);

  logger.printf("[OTA] 完了 (%u bytes) → 再起動\n", (unsigned)written);
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
  const char *url = doc["url"];

  if (!url)
  {
    logger.println("[OTA] url が見つからない");
    jobsReport(job.id, "FAILED", "no url");
    return false;
  }

  // 同一バージョンならスキップ（force=true の場合は無視）
  bool force = doc["force"] | false;
  if (!force && version && strncmp(FIRMWARE_VERSION, version, strlen(version)) == 0)
  {
    logger.printf("[OTA] 同一バージョン (%s)、スキップ\n", version);
    jobsReport(job.id, "SUCCEEDED");
    return false;
  }
  if (force)
    logger.println("[OTA] force=true: バージョンチェックをスキップ");

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
