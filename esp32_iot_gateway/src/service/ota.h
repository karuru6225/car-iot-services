#pragma once
#include <Arduino.h>
#include "jobs.h"

class Ota
{
public:
  // NVS に保存された前回 OTA ジョブの結果を報告する（setup 時に呼ぶ）
  void reportPendingJobResult();

  // OTA で起動した場合に起動成功を確定する
  void confirmBoot();

  // OTA ジョブを実行する。apply() は成功時に esp_restart() するため戻らない
  bool handleJob(const JobInfo &job);

  // URL からファームウェアをダウンロードして書き込み、再起動する
  bool apply(const char *url, const char *jobId);
};

extern Ota ota;
