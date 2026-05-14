#pragma once
#include <stddef.h>
#include <stdint.h>

// SPIFFS に起動ごと1ファイルのデバッグログを保存する。
// ファイル名は /log_<UNIX時間>.txt、最大 LOG_MAX_FILES ファイルのリングバッファ。
// getDebugLogEnabled() が false の場合、各関数は即リターンする。

static const int LOG_MAX_FILES = 12;

void logStorageInit();                            // 起動時に呼ぶ（古いファイル削除・新規作成）
void logStorageWrite(const char *msg);            // logger からフック（内部で logger を呼ばない）
void logStorageClear();                           // 全ログファイルを削除
