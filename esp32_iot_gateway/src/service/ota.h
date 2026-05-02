#pragma once
#include <Arduino.h>

class Ota
{
public:
  // OTA 通知を待ち受け、URL があれば apply() を呼ぶ
  // apply() は成功時に esp_restart() するため戻らない
  // 更新なし / 失敗時は false を返す
  bool check();

  // URL からファームウェアをダウンロードして書き込み、再起動する
  // 成功時は戻らない。失敗時は false を返す
  bool apply(const char *url);

  // OTA で起動した場合に起動成功を確定する
  // ESP_OTA_IMG_PENDING_VERIFY 状態のときのみ動作（通常起動時は何もしない）
  void confirmBoot();

private:
  // NVS に保存された前回ジョブの結果を報告し、起動確認を行う
  void reportPendingJobResult();

  // Jobs ジョブ状態を更新する
  bool updateJobStatus(const char *jobId, const char *status, const char *reason = nullptr);
};

extern Ota ota;
