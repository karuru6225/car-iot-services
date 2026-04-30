#include "lte.h"
#include "logger.h"
#include "device.h"
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

bool Lte::isMqttConnected()
{
  return sendCmdResp("AT+SMSTATE?", 3000).indexOf("+SMSTATE: 1") >= 0;
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

// ─── MQTT ─────────────────────────────────────────────────────────────────

bool Lte::mqttConnect()
{
  String state = sendCmdResp("AT+SMSTATE?", 3000);
  if (state.indexOf("+SMSTATE: 1") >= 0)
    return true;

  const char *mqttHost = getMqttHost();
  if (!mqttHost)
  {
    logger.println("[MQTT] mqtt_host が NVS に未設定");
    return false;
  }

  String urlCmd = "AT+SMCONF=\"URL\",\"";
  urlCmd += mqttHost;
  urlCmd += "\",\"";
  urlCmd += String(MQTT_PORT);
  urlCmd += "\"";
  if (!sendCmd(urlCmd.c_str()))
    return false;

  String idCmd = "AT+SMCONF=\"CLIENTID\",\"";
  idCmd += getDeviceId();
  idCmd += "\"";
  sendCmd(idCmd.c_str());
  sendCmd("AT+SMCONF=\"KEEPTIME\",60");
  sendCmd("AT+SMCONF=\"CLEANSS\",1");
  sendCmd("AT+SMSSL=1,ca.crt,client.crt");
  sendCmd("AT+SMDISC", 3000);

  logger.println("[MQTT] 接続中...");
  if (!sendCmd("AT+SMCONN", 30000))
  {
    logger.println("[MQTT] 接続失敗");
    return false;
  }
  logger.println("[MQTT] 接続完了");
  return true;
}

bool Lte::publish(const char *topic, const char *payload)
{
  if (!mqttConnect())
    return false;

  int len = strlen(payload);
  String cmd = "AT+SMPUB=\"";
  cmd += topic;
  cmd += "\",";
  cmd += len;
  cmd += ",0,0";

  SerialAT.println(cmd);

  unsigned long t = millis();
  String buf = "";
  bool gotPrompt = false;
  while (millis() - t < 3000)
  {
    if (SerialAT.available())
    {
      buf += (char)SerialAT.read();
      if (buf.indexOf('>') >= 0)
      {
        gotPrompt = true;
        break;
      }
    }
  }
  if (!gotPrompt)
  {
    logger.println("[MQTT] publish: no prompt");
    return false;
  }

  SerialAT.print(payload);

  unsigned long t2 = millis();
  String resp = "";
  while (millis() - t2 < 5000)
  {
    if (SerialAT.available())
      resp += (char)SerialAT.read();
    if (resp.indexOf("OK") >= 0 || resp.indexOf("ERROR") >= 0)
      break;
  }
  bool ok = resp.indexOf("OK") >= 0;
  logger.printf("[MQTT] publish %s  %s\n", ok ? "OK" : "NG", payload);
  return ok;
}

bool Lte::subscribe(const char *topic)
{
  if (!mqttConnect())
    return false;

  String cmd = "AT+SMSUB=\"";
  cmd += topic;
  cmd += "\",0";
  bool ok = sendCmd(cmd.c_str());
  logger.printf("[MQTT] subscribe %s: %s\n", topic, ok ? "OK" : "NG");
  return ok;
}

bool Lte::pollMqtt(char *outTopic, int topicSize,
                   char *outPayload, int payloadSize,
                   uint32_t timeoutMs)
{
  unsigned long deadline = millis() + timeoutMs;
  String buf = "";

  while (millis() < deadline)
  {
    while (SerialAT.available())
      buf += (char)SerialAT.read();

    int idx = buf.indexOf("+SMSUB:");
    if (idx < 0)
    {
      delay(10);
      continue;
    }

    // +SMSUB: "topic",length\n
    int q1 = buf.indexOf('"', idx + 7);
    int q2 = buf.indexOf('"', q1 + 1);
    int comma = buf.indexOf(',', q2 + 1);
    int nl = buf.indexOf('\n', comma + 1);
    if (q1 < 0 || q2 < 0 || comma < 0 || nl < 0)
    {
      delay(10);
      continue;
    }

    int payloadLen = buf.substring(comma + 1, nl).toInt();

    // ペイロード全体が届くまで待つ
    while ((int)buf.length() < nl + 1 + payloadLen && millis() < deadline)
    {
      while (SerialAT.available())
        buf += (char)SerialAT.read();
      delay(5);
    }

    if (outTopic && topicSize > 0)
    {
      int len = min(q2 - q1 - 1, topicSize - 1);
      strncpy(outTopic, buf.c_str() + q1 + 1, len);
      outTopic[len] = '\0';
    }
    if (outPayload && payloadSize > 0)
    {
      int len = min(payloadLen, payloadSize - 1);
      strncpy(outPayload, buf.c_str() + nl + 1, len);
      outPayload[len] = '\0';
    }

    logger.printf("[MQTT] recv(%d bytes): %.100s\n",
                  payloadLen, buf.c_str() + nl + 1);
    return true;
  }
  return false;
}

// ─── HTTPS ダウンロード（OTA 用）────────────────────────────────────────

bool Lte::parseUrl(const char *url,
                   char *host, int hostSize,
                   int &port,
                   char *path, int pathSize)
{
  const char *p = url;
  if (strncmp(p, "https://", 8) == 0)
  {
    port = 443;
    p += 8;
  }
  else if (strncmp(p, "http://", 7) == 0)
  {
    port = 80;
    p += 7;
  }
  else
    return false;

  const char *hostEnd = p;
  while (*hostEnd && *hostEnd != '/' && *hostEnd != ':')
    hostEnd++;

  int hostLen = hostEnd - p;
  if (hostLen <= 0 || hostLen >= hostSize)
    return false;
  strncpy(host, p, hostLen);
  host[hostLen] = '\0';
  p = hostEnd;

  if (*p == ':')
  {
    p++;
    port = atoi(p);
    while (*p && *p != '/')
      p++;
  }

  strncpy(path, *p ? p : "/", pathSize - 1);
  path[pathSize - 1] = '\0';
  return true;
}

int Lte::httpGetOta(const char *url,
                    std::function<bool(const uint8_t *, size_t)> onChunk)
{
  char host[128];
  char path[768]; // S3 署名付き URL は長い
  int port = 443;
  if (!parseUrl(url, host, sizeof(host), port, path, sizeof(path)))
  {
    logger.println("[OTA] URL パースエラー");
    return 0;
  }

  logger.printf("[OTA] 接続: %s:%d\n", host, port);

  // MQTT とは独立した TLS 接続（SIM7080G は同時接続可能）
  // モデム内蔵 CA ストアで検証。Amazon Root CA が含まれない場合は
  // SPIFFS の /certs/ca.crt に S3 の Root CA を追加して uploadCert() で登録すること
  TinyGsmClientSecure client(_modem);
  if (!client.connect(host, port))
  {
    logger.println("[OTA] HTTP 接続失敗");
    return 0;
  }

  client.printf("GET %s HTTP/1.0\r\n", path);
  client.printf("Host: %s\r\n", host);
  client.print("Connection: close\r\n\r\n");

  // ステータス行を読む
  unsigned long t = millis();
  String statusLine = "";
  while (millis() - t < 10000)
  {
    while (client.available())
    {
      char c = client.read();
      if (c == '\n')
        goto statusDone;
      if (c != '\r')
        statusLine += c;
    }
    if (!client.connected())
      break;
    delay(1);
  }
statusDone:
  int statusCode = 0;
  if (statusLine.startsWith("HTTP/"))
    statusCode = statusLine.substring(9, 12).toInt();

  logger.printf("[OTA] HTTP %d\n", statusCode);
  if (statusCode != 200)
  {
    client.stop();
    return statusCode;
  }

  // 残りのヘッダーを読み飛ばす（空行まで）
  String line = "";
  while (millis() - t < 15000)
  {
    while (client.available())
    {
      char c = client.read();
      if (c == '\n')
      {
        if (line.length() == 0)
          goto headerDone;
        line = "";
      }
      else if (c != '\r')
      {
        line += c;
      }
    }
    if (!client.connected())
      break;
    delay(1);
  }
headerDone:

  // ボディをチャンク単位で読んで onChunk に渡す
  uint8_t buf[512];
  while (true)
  {
    unsigned long chunkStart = millis();
    int len = 0;
    while (len < (int)sizeof(buf))
    {
      if (client.available())
      {
        buf[len++] = client.read();
        chunkStart = millis();
      }
      else if (!client.connected())
      {
        break;
      }
      else if (millis() - chunkStart > 10000)
      {
        logger.println("[OTA] ダウンロードタイムアウト");
        client.stop();
        return 0;
      }
      else
      {
        delay(1);
      }
    }
    if (len > 0)
    {
      if (!onChunk(buf, len))
      {
        client.stop();
        return 0;
      }
    }
    if (!client.connected() && !client.available())
      break;
  }

  client.stop();
  return 200;
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
