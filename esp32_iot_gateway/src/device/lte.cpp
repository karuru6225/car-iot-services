#include "lte.h"
#include "../service/logger.h"
#include "../config.h"
#include <esp_rom_crc.h>
#include <SPIFFS.h>

Lte lte;

static String readSpiffsFile(const char *path)
{
  File f = SPIFFS.open(path, "r");
  if (!f)
    return String();
  String s = f.readString();
  f.close();
  return s;
}

// ─── AT コマンドユーティリティ ────────────────────────────────────────────

String Lte::sendCmdResp(const char *cmd, uint32_t timeoutMs)
{
  SerialAT.println(cmd);
  unsigned long t = millis();
  String resp = "";
  while (millis() - t < timeoutMs)
  {
    if (SerialAT.available())
      resp += (char)SerialAT.read();
    if (resp.indexOf("OK") >= 0 || resp.indexOf("ERROR") >= 0)
      break;
  }
  resp.trim();
  logger.printf("  [AT] %s => %s\n", cmd, resp.c_str());
  return resp;
}

bool Lte::sendCmd(const char *cmd, uint32_t timeoutMs)
{
  return sendCmdResp(cmd, timeoutMs).indexOf("OK") >= 0;
}

// ─── 接続管理 ─────────────────────────────────────────────────────────────

bool Lte::connect()
{
  logger.println("[LTE] ネットワーク登録待ち（最大60秒）...");
  if (!_modem.waitForNetwork(60000))
  {
    logger.println("[LTE] ネットワーク登録失敗");
    return false;
  }
  logger.printf("[LTE] Signal: %d\n", _modem.getSignalQuality());
  if (!_modem.gprsConnect(APN, APN_USER, APN_PASS))
  {
    logger.println("[LTE] GPRS 接続失敗");
    return false;
  }
  logger.printf("[LTE] IP: %s\n", _modem.localIP().toString().c_str());
  return true;
}

void Lte::disconnect()
{
  sendCmd("AT+SMDISC", 5000);
  _modem.gprsDisconnect();
}

void Lte::radioOff()
{
  sendCmd("AT+CFUN=0", 5000);
  digitalWrite(LTE_EN_PIN, LOW);
  logger.println("[LTE] 電源オフ");
}

void Lte::powerOff()
{
  String resp = sendCmdResp("AT+CPOWD=1", 5000);
  if (resp.indexOf("NORMAL POWER DOWN") >= 0)
    logger.println("[LTE] 電源オフ（正常シャットダウン）");
  else
    logger.println("[LTE] 電源オフ（強制切断）");
  digitalWrite(LTE_EN_PIN, LOW);
}

bool Lte::isConnected()
{
  return _modem.isGprsConnected();
}

// ─── 証明書アップロード ───────────────────────────────────────────────────

void Lte::uploadCert(const char *filename, const char *pem)
{
  int len = strlen(pem);
  SerialAT.printf("AT+CFSWFILE=3,\"%s\",0,%d,5000\r\n", filename, len);

  unsigned long t = millis();
  String buf = "";
  bool gotPrompt = false;
  while (millis() - t < 5000)
  {
    if (SerialAT.available())
    {
      buf += (char)SerialAT.read();
      if (buf.indexOf("DOWNLOAD") >= 0)
      {
        gotPrompt = true;
        break;
      }
    }
  }
  if (!gotPrompt)
  {
    logger.printf("  [CERT] %s: no prompt\n", filename);
    return;
  }

  SerialAT.print(pem);

  unsigned long t2 = millis();
  String resp = "";
  while (millis() - t2 < 10000)
  {
    if (SerialAT.available())
      resp += (char)SerialAT.read();
    if (resp.indexOf("OK") >= 0 || resp.indexOf("ERROR") >= 0)
      break;
  }
  logger.printf("  [CERT] %s (%d bytes) => %s\n",
                filename, len, resp.indexOf("OK") >= 0 ? "OK" : "NG");
}

bool Lte::readFile(const char *filepath, std::function<bool(const uint8_t *, size_t)> onChunk)
{
  // /customer/ プレフィックスを除去（index=3 固定）
  static const char PREFIX[] = "/customer/";
  const char *name = (strncmp(filepath, PREFIX, sizeof(PREFIX) - 1) == 0)
                       ? filepath + sizeof(PREFIX) - 1
                       : filepath;

  sendCmd("AT+CFSINIT");

  // ファイルサイズ確認
  {
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "AT+CFSGFIS=3,\"%s\"", name);
    String resp = sendCmdResp(cmd, 5000);
    if (resp.indexOf("ERROR") >= 0)
    {
      logger.printf("[FILE] %s: not found\n", filepath);
      sendCmd("AT+CFSTERM");
      return false;
    }
    int q1 = resp.indexOf(':');
    if (q1 < 0) { sendCmd("AT+CFSTERM"); return false; }
    int fileSize = resp.substring(q1 + 1).toInt();
    if (fileSize <= 0) { sendCmd("AT+CFSTERM"); return false; }
    logger.printf("[FILE] %s: %d bytes\n", filepath, fileSize);

    // AT+CFSRFILE=<dir>,<name>,<mode>,<size>,<pos>
    // mode=1: 指定位置から読み取り。バイナリ対応のため readBytes を使う
    static const int CHUNK = 4096;
    static uint8_t   chunk[CHUNK];
    int offset = 0;

    while (offset < fileSize)
    {
      int readLen = min(CHUNK, fileSize - offset);
      char rcmd[128];
      snprintf(rcmd, sizeof(rcmd), "AT+CFSRFILE=3,\"%s\",1,%d,%d", name, readLen, offset);

      logger.printf("  [AT] %s\n", rcmd);
      SerialAT.println(rcmd);

      // +CFSRFILE: <actual>\r\n<data> を受信
      unsigned long deadline = millis() + 10000;
      String        header   = "";
      int           idx = -1, nl = -1;
      while (millis() < deadline)
      {
        while (SerialAT.available())
          header += (char)SerialAT.read();
        idx = header.indexOf("+CFSRFILE:");
        if (idx >= 0)
        {
          nl = header.indexOf('\n', idx);
          if (nl >= 0) break;
        }
        delay(5);
      }
      if (idx < 0 || nl < 0)
      {
        logger.printf("[FILE] CFSRFILE timeout at %d\n", offset);
        sendCmd("AT+CFSTERM");
        return false;
      }

      int actual = header.substring(idx + 10, nl).toInt();
      if (actual <= 0)
      {
        logger.printf("[FILE] CFSRFILE size=0 at %d\n", offset);
        sendCmd("AT+CFSTERM");
        return false;
      }

      // 先行データをコピー
      int preloaded = 0;
      int preStart  = nl + 1;
      int preAvail  = min((int)header.length() - preStart, actual);
      for (int i = 0; i < preAvail; i++)
        chunk[preloaded++] = (uint8_t)header[preStart + i];

      // 残りを readBytes で読む（\0 に安全）
      int received = preloaded;
      if (received < actual)
      {
        SerialAT.setTimeout(10000);
        received += SerialAT.readBytes((char *)chunk + preloaded, actual - preloaded);
      }

      // 末尾の OK を読み捨て
      {
        unsigned long t      = millis();
        String        trail  = "";
        while (millis() - t < 3000)
        {
          while (SerialAT.available())
            trail += (char)SerialAT.read();
          if (trail.indexOf("OK") >= 0) break;
          delay(5);
        }
      }

      if (received <= 0) { sendCmd("AT+CFSTERM"); return false; }
      if (!onChunk(chunk, received)) { sendCmd("AT+CFSTERM"); return false; }

      offset += received;
      logger.printf("[FILE] %d / %d bytes\n", offset, fileSize);
    }
  }
  sendCmd("AT+CFSTERM");
  return true;
}

// ─── 時刻同期 ─────────────────────────────────────────────────────────────

bool Lte::syncTime()
{
  String resp = sendCmdResp("AT+CCLK?", 5000);
  int q1 = resp.indexOf('"');
  int q2 = resp.lastIndexOf('"');
  if (q1 < 0 || q2 <= q1)
  {
    logger.println("[TIME] CCLK パース失敗");
    return false;
  }

  String ts = resp.substring(q1 + 1, q2);
  if (ts.length() < 17)
    return false;

  struct tm t2 = {};
  t2.tm_year = ts.substring(0, 2).toInt() + 100;
  t2.tm_mon = ts.substring(3, 5).toInt() - 1;
  t2.tm_mday = ts.substring(6, 8).toInt();
  t2.tm_hour = ts.substring(9, 11).toInt();
  t2.tm_min = ts.substring(12, 14).toInt();
  t2.tm_sec = ts.substring(15, 17).toInt();

  int tzQuarters = 0;
  if (ts.length() > 17)
  {
    char sign = ts.charAt(17);
    tzQuarters = (sign == '+') ? ts.substring(18).toInt()
                               : -ts.substring(18).toInt();
  }

  time_t epoch = mktime(&t2) - (time_t)tzQuarters * 15 * 60;
  struct timeval tv = {epoch, 0};
  settimeofday(&tv, NULL);

  char buf[32];
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", gmtime(&epoch));
  logger.printf("[TIME] 同期完了: %s\n", buf);
  return true;
}

// ─── セットアップ ─────────────────────────────────────────────────────────

void Lte::setup()
{
  pinMode(LTE_EN_PIN, OUTPUT);
  digitalWrite(LTE_EN_PIN, HIGH);
  delay(1000);

  logger.println("[LTE] 初期化...");
  SerialAT.begin(115200, SERIAL_8N1, LTE_RX_PIN, LTE_TX_PIN);

  // unsigned long t = millis();
  // while (millis() - t < 10000)
  // {
  //   while (SerialAT.available())
  //   {
  //     char c[] = {0, 0};
  //     c[0] = (char)SerialAT.read();
  //     logger.print(c);
  //   }
  // }
  delay(3000);
  _modem.init();
  sendCmd("ATE0");
  sendCmd("AT+CSCLK=0");
  for (int i = 0; i < 5; i++)
  {
    if (sendCmd("AT"))
    {
      logger.println("[LTE] モジュール応答 OK");
      break;
    }
    logger.println("[LTE] モジュール応答なし、再試行...");
    delay(2000);
  }
  logger.printf("[LTE] Modem: %s\n", _modem.getModemInfo().c_str());

  sendCmd("AT+CFUN=0", 5000);
  delay(2000);
  sendCmd("AT+CGDCONT=1,\"IP\",\"soracom.io\"");
  sendCmd("AT+CFUN=1", 5000);

  logger.println("[LTE] SMS Ready 待ち...");
  unsigned long t0 = millis();
  while (millis() - t0 < 15000)
  {
    if (SerialAT.available())
    {
      String line = SerialAT.readStringUntil('\n');
      if (line.indexOf("SMS Ready") >= 0)
      {
        logger.println("[LTE] SMS Ready!");
        break;
      }
    }
  }

  if (!SPIFFS.begin())
  {
    logger.println("[LTE] SPIFFS マウント失敗");
    return;
  }

  String caCert = readSpiffsFile(CERT_PATH_CA);
  String devCert = readSpiffsFile(CERT_PATH_DEVICE);
  String devKey = readSpiffsFile(CERT_PATH_KEY);

  if (caCert.isEmpty() || devCert.isEmpty() || devKey.isEmpty())
  {
    logger.printf("[LTE] 証明書ファイルなし（要プロビジョニング: %s, %s, %s）\n",
                  CERT_PATH_CA, CERT_PATH_DEVICE, CERT_PATH_KEY);
    return;
  }

  uint32_t crc = esp_rom_crc32_le(0, (const uint8_t *)caCert.c_str(), caCert.length());
  crc = esp_rom_crc32_le(crc, (const uint8_t *)devCert.c_str(), devCert.length());
  crc = esp_rom_crc32_le(crc, (const uint8_t *)devKey.c_str(), devKey.length());

  uint32_t stored = 0;
  bool needUpload = !getCertCrc(stored) || stored != crc;

  if (needUpload)
  {
    logger.printf("[LTE] 証明書アップロード中... (CRC=0x%08X)\n", crc);
    sendCmd("AT+CFSINIT");
    uploadCert("ca.crt", caCert.c_str());
    uploadCert("client.crt", devCert.c_str());
    uploadCert("client.key", devKey.c_str());
    sendCmd("AT+CFSTERM");
    sendCmdResp("AT+CSSLCFG=\"CONVERT\",2,\"ca.crt\"", 5000);
    sendCmd("AT+CSSLCFG=\"CONVERT\",1,\"client.crt\",\"client.key\"");
    setCertCrc(crc);
    logger.println("[LTE] 証明書アップロード完了");
  }
  else
  {
    logger.printf("[LTE] 証明書アップロードスキップ (CRC=0x%08X 一致)\n", crc);
  }
  sendCmd("AT+CSSLCFG=\"sslversion\",0,3");

  if (connect())
    syncTime();
}
