#pragma once
#include <stddef.h>

struct JobInfo {
  char id[64];
  char operation[32];
  char document[512]; // jobDocument フィールド全体の JSON 文字列
};

// Jobs トピックを subscribe する（setup 時に 1 回呼ぶ）
void jobsSetup();

// 次の pending ジョブを取得する。ジョブがあれば true を返す
bool jobsGetNext(JobInfo &out);

// ジョブの実行状態を報告する。MQTT publish 成功時 true を返す
bool jobsReport(const char *jobId, const char *status, const char *reason = nullptr);
