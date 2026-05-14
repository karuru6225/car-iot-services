#include "log_storage.h"
#include "../config.h"
#include <SPIFFS.h>
#include <time.h>
#include <cstdlib>
#include <cstring>

static const char LOG_PREFIX[] = "/log_";
static char       s_currentPath[32] = {};

// ─── ファイル名ユーティリティ ──────────────────────────────────────────────────

static bool isLogFile(const char *name)
{
  return strncmp(name, LOG_PREFIX, sizeof(LOG_PREFIX) - 1) == 0;
}

static unsigned long tsFromName(const char *name)
{
  return strtoul(name + sizeof(LOG_PREFIX) - 1, nullptr, 10);
}

static void buildPath(unsigned long ts, char *buf, size_t len)
{
  snprintf(buf, len, "/log_%lu.txt", ts);
}

// ─── ファイル一覧収集（昇順ソート済み）────────────────────────────────────────

static int collectLogFiles(unsigned long *ts, int maxCount)
{
  int count = 0;
  File root = SPIFFS.open("/");
  if (!root) return 0;

  File f = root.openNextFile();
  while (f && count < maxCount)
  {
    if (isLogFile(f.name()))
      ts[count++] = tsFromName(f.name());
    f = root.openNextFile();
  }

  // 昇順ソート（バブル）
  for (int i = 0; i < count - 1; i++)
    for (int j = 0; j < count - i - 1; j++)
      if (ts[j] > ts[j + 1])
      {
        unsigned long tmp = ts[j];
        ts[j] = ts[j + 1];
        ts[j + 1] = tmp;
      }
  return count;
}

// ─── 初期化 ───────────────────────────────────────────────────────────────────

void logStorageInit()
{
  s_currentPath[0] = '\0';
  if (!getDebugLogEnabled()) return;

  if (!SPIFFS.begin(false))
  {
    Serial.println("[LOG_STORE] SPIFFS begin 失敗");
    return;
  }

  unsigned long timestamps[LOG_MAX_FILES + 2];
  int count = collectLogFiles(timestamps, sizeof(timestamps) / sizeof(timestamps[0]));

  // 上限を超えていたら古いものから削除
  while (count >= LOG_MAX_FILES)
  {
    char path[32];
    buildPath(timestamps[0], path, sizeof(path));
    SPIFFS.remove(path);
    for (int i = 0; i < count - 1; i++) timestamps[i] = timestamps[i + 1];
    count--;
  }

  // 現在の起動時刻でファイルを新規作成
  time_t now = time(nullptr);
  unsigned long ts = (now > 1577836800L) ? (unsigned long)now : (unsigned long)(millis() / 1000UL);
  buildPath(ts, s_currentPath, sizeof(s_currentPath));

  File f = SPIFFS.open(s_currentPath, FILE_WRITE);
  if (f)
    f.close();
  else
    s_currentPath[0] = '\0';

  Serial.printf("[LOG_STORE] 初期化: %s (既存 %d ファイル)\n", s_currentPath, count);
}

// ─── 書き込み ─────────────────────────────────────────────────────────────────

void logStorageWrite(const char *msg)
{
  if (s_currentPath[0] == '\0') return;

  char timebuf[28];
  time_t now = time(nullptr);
  if (now > 1577836800L)
  {
    struct tm *t = gmtime(&now);
    strftime(timebuf, sizeof(timebuf), "[%Y-%m-%dT%H:%M:%SZ] ", t);
  }
  else
  {
    snprintf(timebuf, sizeof(timebuf), "[+%lus] ", millis() / 1000UL);
  }

  File f = SPIFFS.open(s_currentPath, FILE_APPEND);
  if (!f) return;
  f.print(timebuf);
  f.print(msg);
  f.print('\n');
  f.close();
}

// ─── クリア ───────────────────────────────────────────────────────────────────

void logStorageClear()
{
  if (!SPIFFS.begin(false)) return;

  unsigned long timestamps[LOG_MAX_FILES + 2];
  int count = collectLogFiles(timestamps, sizeof(timestamps) / sizeof(timestamps[0]));

  for (int i = 0; i < count; i++)
  {
    char path[32];
    buildPath(timestamps[i], path, sizeof(path));
    SPIFFS.remove(path);
  }
  s_currentPath[0] = '\0';
  Serial.printf("[LOG_STORE] %d ファイル削除\n", count);
}
