#include "https.h"
#include "../device/lte.h"
#include "logger.h"

Https https;

// URL を https://host/path に分解する
static bool parseUrl(const char *url,
                     char *host, int hostSize,
                     char *path, int pathSize)
{
  const char *p = url;
  if (strncmp(p, "https://", 8) == 0)
    p += 8;
  else if (strncmp(p, "http://", 7) == 0)
    p += 7;
  else
    return false;

  const char *slash = strchr(p, '/');
  if (!slash)
  {
    strncpy(host, p, hostSize - 1);
    host[hostSize - 1] = '\0';
    strncpy(path, "/", pathSize - 1);
    path[pathSize - 1] = '\0';
  }
  else
  {
    int len = slash - p;
    if (len >= hostSize)
      return false;
    strncpy(host, p, len);
    host[len] = '\0';
    strncpy(path, slash, pathSize - 1);
    path[pathSize - 1] = '\0';
  }
  return true;
}

// SerialAT から +SHREAD: の行を読んでデータ長を返す。
// ヘッダ行より後ろに先行データがあれば chunk に先コピーして preloaded に返す。
static int waitShreadHeader(uint8_t *chunk, int chunkSize,
                            int &preloaded, uint32_t timeoutMs)
{
  unsigned long deadline = millis() + timeoutMs;
  String header = "";
  int idx = -1, nl = -1;

  while (millis() < deadline)
  {
    while (SerialAT.available())
      header += (char)SerialAT.read();

    idx = header.indexOf("+SHREAD:");
    if (idx >= 0)
    {
      nl = header.indexOf('\n', idx);
      if (nl >= 0)
        break;
    }
    delay(5);
  }

  if (idx < 0 || nl < 0)
    return -1;

  int actual = header.substring(idx + 8, nl).toInt();
  if (actual <= 0)
    return -1;

  // ヘッダ行の後ろに先行して入ってきたデータをコピー
  preloaded = 0;
  int preStart = nl + 1;
  int preAvail = min((int)header.length() - preStart, min(actual, chunkSize));
  for (int i = 0; i < preAvail; i++)
    chunk[preloaded++] = (uint8_t)header[preStart + i];

  return actual;
}

int Https::get(const char *url,
               std::function<bool(const uint8_t *, size_t)> onChunk)
{
  char host[128];
  char path[768];
  if (!parseUrl(url, host, sizeof(host), path, sizeof(path)))
  {
    logger.println("[HTTPS] URL パースエラー");
    return 0;
  }
  logger.printf("[HTTPS] 接続: %s\n", host);

  // AT+SH* 設定（ctxindex=1: ctxindex=0 は MQTT が使用）
  // ctxindex=1 に TLS 1.2 を明示設定（未設定だと S3 との TLS ハンドシェイクが失敗する）
  // AT+SMSSL=<index>: 0=SSL無効, 1-6 = CSSLCFG ctindex 0-5
  // MQTT は AT+SMSSL=1 → CSSLCFG ctindex 0 を使用。
  // HTTPS は AT+SHSSL=2 → CSSLCFG ctindex 1 を使用（競合回避）。
  // ctindex 1 に TLS 1.2 を明示設定（MQTT 用 ctindex 0 とは別設定が必要）。
  lte.sendCmd("AT+CSSLCFG=\"sslversion\",1,3");
  lte.sendCmd("AT+SHSSL=2,\"ca.crt\"");
  {
    char cmd[160];
    snprintf(cmd, sizeof(cmd), "AT+SHCONF=\"URL\",\"https://%s\"", host);
    lte.sendCmd(cmd);
  }
  lte.sendCmd("AT+SHCONF=\"BODYLEN\",4096");
  lte.sendCmd("AT+SHCONF=\"HEADERLEN\",350");

  if (!lte.sendCmd("AT+SHCONN", 30000))
  {
    logger.println("[HTTPS] 接続失敗");
    lte.sendCmd("AT+SHDISC");
    return 0;
  }

  // GET リクエスト（非同期 — +SHREQ: URC で結果が来る）
  // type: 1=GET, 2=PUT, 3=POST, 4=PATCH, 5=HEAD
  {
    char cmd[800];
    snprintf(cmd, sizeof(cmd), "AT+SHREQ=\"%s\",1", path);
    lte.sendCmd(cmd, 5000);
  }

  // +SHREQ: "GET",<status>,<dataLen> を受信
  int statusCode = 0;
  int32_t dataLen = 0;
  {
    unsigned long deadline = millis() + 30000;
    String buf = "";
    while (millis() < deadline)
    {
      while (SerialAT.available())
        buf += (char)SerialAT.read();

      int idx = buf.indexOf("+SHREQ:");
      if (idx >= 0)
      {
        int c1 = buf.indexOf(',', idx + 7); // "GET" の後の ,
        int c2 = buf.indexOf(',', c1 + 1);  // status の後の ,
        int nl = buf.indexOf('\n', c2 + 1);
        if (c1 >= 0 && c2 >= 0 && nl >= 0)
        {
          statusCode = buf.substring(c1 + 1, c2).toInt();
          dataLen = buf.substring(c2 + 1, nl).toInt();
          break;
        }
      }
      delay(10);
    }
  }

  logger.printf("[HTTPS] HTTP %d  size: %d bytes\n", statusCode, (int)dataLen);
  if (statusCode != 200)
  {
    lte.sendCmd("AT+SHDISC");
    return statusCode ? statusCode : 0;
  }

  // AT+SHREAD=offset,size でチャンク読み取り
  // datalen > 2048 は複数 URC に分割されるため 2048 以下に抑える
  static const int32_t CHUNK = 2048;
  static uint8_t chunk[CHUNK];
  int32_t offset = 0;

  while (offset < dataLen)
  {
    int32_t readLen = min(CHUNK, dataLen - offset);

    char cmd[40];
    snprintf(cmd, sizeof(cmd), "AT+SHREAD=%d,%d", (int)offset, (int)readLen);
    SerialAT.println(cmd);

    // +SHREAD: <len>\r\n<data>\r\nOK の受信
    int preloaded = 0;
    int actual = waitShreadHeader(chunk, (int)readLen, preloaded, 10000);
    if (actual <= 0)
    {
      logger.printf("[HTTPS] SHREAD 失敗 at %d\n", (int)offset);
      lte.sendCmd("AT+SHDISC");
      return 0;
    }

    // 先行データの後ろを readBytes で読む
    int received = preloaded;
    if (received < actual)
    {
      SerialAT.setTimeout(10000);
      received += SerialAT.readBytes((char *)chunk + preloaded, actual - preloaded);
    }

    // 末尾の OK を読み捨て
    {
      unsigned long t = millis();
      String trailing = "";
      while (millis() - t < 3000)
      {
        while (SerialAT.available())
          trailing += (char)SerialAT.read();
        if (trailing.indexOf("OK") >= 0)
          break;
        delay(5);
      }
    }

    if (received <= 0)
    {
      logger.printf("[HTTPS] データ受信失敗 at %d\n", (int)offset);
      lte.sendCmd("AT+SHDISC");
      return 0;
    }

    if (!onChunk(chunk, received))
    {
      lte.sendCmd("AT+SHDISC");
      return 0;
    }

    offset += received;
    logger.printf("[HTTPS] %d / %d bytes\n", (int)offset, (int)dataLen);
  }

  lte.sendCmd("AT+SHDISC");
  return 200;
}

int Https::download(const char *url, const char *filename)
{
  logger.printf("[HTTPS] ダウンロード: %s → /customer/%s\n", url, filename);

  // AT+HTTPTOFS=<url>,<file_path>[,<timeout>]
  // タイムアウトは 300 秒（1MB 超のファームウェアを想定）
  char cmd[900];
  snprintf(cmd, sizeof(cmd), "AT+HTTPTOFS=\"%s\",\"/customer/%s\",300", url, filename);

  // OK を待ってから +HTTPTOFS: URC を別途受信（SHREQ と同パターン）
  lte.sendCmd(cmd, 5000);

  int statusCode = 0;
  int32_t dataLen = 0;
  {
    unsigned long deadline = millis() + 330000UL;
    String buf = "";
    while (millis() < deadline)
    {
      while (SerialAT.available())
        buf += (char)SerialAT.read();

      int idx = buf.indexOf("+HTTPTOFS:");
      if (idx >= 0)
      {
        int c1 = buf.indexOf(',', idx + 10);
        int nl = buf.indexOf('\n', idx + 10);
        if (c1 >= 0 && nl >= 0)
        {
          statusCode = buf.substring(idx + 10, c1).toInt();
          dataLen = buf.substring(c1 + 1, nl).toInt();
          break;
        }
      }
      delay(100);
    }
  }

  logger.printf("[HTTPS] HTTP %d  size: %d bytes\n", statusCode, (int)dataLen);
  if (statusCode != 200)
  {
    logger.printf("[HTTPS] ダウンロード失敗 (HTTP %d)\n", statusCode);
    return -1;
  }
  return (int)dataLen;
}

int Https::put(const char *url, const uint8_t *data, size_t len)
{
  char host[128];
  char path[768];
  if (!parseUrl(url, host, sizeof(host), path, sizeof(path)))
  {
    logger.println("[HTTPS] PUT: URL パースエラー");
    return 0;
  }
  if (len == 0 || len > 4096)
  {
    logger.printf("[HTTPS] PUT: body length %u は範囲外 (1-4096)\n", (unsigned)len);
    return 0;
  }
  logger.printf("[HTTPS] PUT: %s (%u bytes)\n", host, (unsigned)len);

  // AT+SMSSL=<index>: 0=SSL無効, 1-6 = CSSLCFG ctindex 0-5
  // MQTT は AT+SMSSL=1 → CSSLCFG ctindex 0 を使用。
  // HTTPS は AT+SHSSL=2 → CSSLCFG ctindex 1 を使用（競合回避）。
  // ctindex 1 に TLS 1.2 を明示設定（MQTT 用 ctindex 0 とは別設定が必要）。
  lte.sendCmd("AT+CSSLCFG=\"sslversion\",1,3");
  lte.sendCmd("AT+SHSSL=2,\"ca.crt\"");
  {
    char cmd[160];
    snprintf(cmd, sizeof(cmd), "AT+SHCONF=\"URL\",\"https://%s\"", host);
    lte.sendCmd(cmd);
  }
  // BODYLEN はデータサイズに合わせて動的に設定
  {
    char cmd[40];
    snprintf(cmd, sizeof(cmd), "AT+SHCONF=\"BODYLEN\",%u", (unsigned)len);
    lte.sendCmd(cmd);
  }
  lte.sendCmd("AT+SHCONF=\"HEADERLEN\",350");

  if (!lte.sendCmd("AT+SHCONN", 30000))
  {
    logger.println("[HTTPS] PUT: 接続失敗");
    lte.sendCmd("AT+SHDISC");
    return 0;
  }

  // AT+SHBOD: データ入力モード開始
  // len_body に実際のサイズを指定することで CR(0x0D) を含むバイナリも正しく受け付ける。
  // モデムは OK ではなく '>' プロンプトを返すため sendCmd は使わず自前で待つ。
  {
    char shbodCmd[32];
    snprintf(shbodCmd, sizeof(shbodCmd), "AT+SHBOD=%u,10000", (unsigned)len);
    SerialAT.println(shbodCmd);
    logger.printf("  [AT] %s\n", shbodCmd);

    // '>' プロンプトを待つ
    unsigned long t = millis();
    String buf = "";
    bool prompted = false;
    while (millis() - t < 5000)
    {
      while (SerialAT.available()) buf += (char)SerialAT.read();
      if (buf.indexOf('>') >= 0) { prompted = true; break; }
      if (buf.indexOf("ERROR") >= 0) break;
      delay(10);
    }
    if (!prompted)
    {
      logger.println("[HTTPS] PUT: SHBOD prompt timeout");
      lte.sendCmd("AT+SHDISC");
      return 0;
    }
  }

  // データ送信 → Ctrl-Z（0x1A）で確定
  SerialAT.write(data, len);
  SerialAT.write((uint8_t)0x1A);

  // SHBOD 確定応答（OK or ERROR）待ち
  {
    unsigned long t = millis();
    String buf = "";
    while (millis() - t < 10000)
    {
      while (SerialAT.available())
        buf += (char)SerialAT.read();
      if (buf.indexOf("OK") >= 0 || buf.indexOf("ERROR") >= 0) break;
      delay(10);
    }
    if (buf.indexOf("OK") < 0)
    {
      logger.println("[HTTPS] PUT: SHBOD error");
      lte.sendCmd("AT+SHDISC");
      return 0;
    }
  }

  // PUT リクエスト送信
  {
    char cmd[800];
    snprintf(cmd, sizeof(cmd), "AT+SHREQ=\"%s\",2", path);
    lte.sendCmd(cmd, 5000);
  }

  // +SHREQ: "PUT",<status>,<dataLen> 受信
  int statusCode = 0;
  {
    unsigned long deadline = millis() + 30000;
    String buf = "";
    while (millis() < deadline)
    {
      while (SerialAT.available())
        buf += (char)SerialAT.read();
      int idx = buf.indexOf("+SHREQ:");
      if (idx >= 0)
      {
        int c1 = buf.indexOf(',', idx + 7);
        int c2 = buf.indexOf(',', c1 + 1);
        int nl = buf.indexOf('\n', c2 + 1);
        if (c1 >= 0 && c2 >= 0 && nl >= 0)
        {
          statusCode = buf.substring(c1 + 1, c2).toInt();
          break;
        }
      }
      delay(10);
    }
  }

  logger.printf("[HTTPS] PUT HTTP %d\n", statusCode);
  lte.sendCmd("AT+SHDISC");
  return statusCode;
}