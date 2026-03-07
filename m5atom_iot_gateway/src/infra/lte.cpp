#include "lte.h"
#include "logger.h"
#include "certs.h"

Lte lte;

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
  logger.println("[LTE] ラジオオフ");
}

bool Lte::isConnected()
{
  return _modem.isGprsConnected();
}

// 証明書ファイルをモデムのファイルシステムに書き込む（AT+CFSWFILE）
void Lte::uploadCert(const char *filename, const char *pem)
{
  int len = strlen(pem);
  // index=3(customer), mode=0(新規作成), inputtime=5000ms
  SerialAT.printf("AT+CFSWFILE=3,\"%s\",0,%d,5000\r\n", filename, len);

  // "DOWNLOAD" プロンプト待ち（最大5秒）
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

  // OK 待ち
  unsigned long t2 = millis();
  String resp = "";
  while (millis() - t2 < 10000)
  {
    if (SerialAT.available())
      resp += (char)SerialAT.read();
    if (resp.indexOf("OK") >= 0 || resp.indexOf("ERROR") >= 0)
      break;
  }
  bool ok = resp.indexOf("OK") >= 0;
  logger.printf("  [CERT] %s (%d bytes) => %s\n", filename, len, ok ? "OK" : "NG");
}

bool Lte::mqttConnect()
{
  // 接続済み確認
  String state = sendCmdResp("AT+SMSTATE?", 3000);
  if (state.indexOf("+SMSTATE: 1") >= 0)
    return true;

  // ブローカー・クライアント設定
  String urlCmd = "AT+SMCONF=\"URL\",\"";
  urlCmd += MQTT_HOST;
  urlCmd += "\",\"8883\"";
  if (!sendCmd(urlCmd.c_str()))
    return false;

  String idCmd = "AT+SMCONF=\"CLIENTID\",\"";
  idCmd += MQTT_CLIENT_ID;
  idCmd += "\"";
  sendCmd(idCmd.c_str());
  sendCmd("AT+SMCONF=\"KEEPTIME\",60");
  sendCmd("AT+SMCONF=\"CLEANSS\",1");

  // SSL 有効化（CA LIST + CERT NAME、クォートなし）
  sendCmd("AT+SMSSL=1,ca.crt,client.crt");

  // 残存接続をクリアしてから接続
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

  // ">" プロンプト待ち（最大3秒）
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

// AT+CCLK? でネットワーク時刻を取得し、ESP32 RTC を UTC に同期する
// レスポンス形式: +CCLK: "YY/MM/DD,HH:MM:SS+TZ"  (+TZ は 1/4時間単位)
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
  String ts = resp.substring(q1 + 1, q2); // "25/01/15,12:00:00+36"
  if (ts.length() < 17)
    return false;

  struct tm t2 = {};
  t2.tm_year = ts.substring(0, 2).toInt() + 100; // 2000年基準 → 1900年基準
  t2.tm_mon  = ts.substring(3, 5).toInt() - 1;
  t2.tm_mday = ts.substring(6, 8).toInt();
  t2.tm_hour = ts.substring(9, 11).toInt();
  t2.tm_min  = ts.substring(12, 14).toInt();
  t2.tm_sec  = ts.substring(15, 17).toInt();

  // タイムゾーンオフセット（1/4時間単位）を UTC に換算
  int tzQuarters = 0;
  if (ts.length() > 17)
  {
    char sign = ts.charAt(17);
    tzQuarters = (sign == '+') ? ts.substring(18).toInt()
                               : -ts.substring(18).toInt();
  }

  // ESP32 の mktime() は TZ 未設定時 UTC 扱い → epoch は UTC になる
  time_t epoch = mktime(&t2) - (time_t)tzQuarters * 15 * 60;
  struct timeval tv = {epoch, 0};
  settimeofday(&tv, NULL);

  char buf[32];
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", gmtime(&epoch));
  logger.printf("[TIME] 同期完了: %s\n", buf);
  return true;
}

void Lte::setup()
{
  logger.println("[LTE] 初期化...");
  SerialAT.begin(115200, SERIAL_8N1, LTE_RX_PIN, LTE_TX_PIN);
  delay(3000);
  _modem.init();
  sendCmd("ATE0");         // エコー無効化
  sendCmd("AT+CSCLK=0"); // スロークロック無効化（NVRAM 保存値を上書き）
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

  // APN 設定（CFUN=0 → CGDCONT → CFUN=1 の順序必須）
  sendCmd("AT+CFUN=0", 5000);
  delay(2000);
  sendCmd("AT+CGDCONT=1,\"IP\",\"soracom.io\"");
  sendCmd("AT+CFUN=1", 5000);

  // SMS Ready 待ち（最大15秒）
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

  // 証明書をモデムのファイルシステムに書き込む
  logger.println("[LTE] 証明書アップロード中...");
  sendCmd("AT+CFSINIT");
  uploadCert("ca.crt", AWS_ROOT_CA);
  uploadCert("client.crt", DEVICE_CERT);
  uploadCert("client.key", DEVICE_KEY);
  sendCmd("AT+CFSTERM");
  logger.println("[LTE] 証明書アップロード完了");

  // ファイル存在確認（デバッグ用）
  sendCmdResp("AT+CFSGFIS=3,\"ca.crt\"", 3000);
  sendCmdResp("AT+CFSGFIS=3,\"client.crt\"", 3000);
  sendCmdResp("AT+CFSGFIS=3,\"client.key\"", 3000);

  // SSL コンテキスト 0 に証明書を設定
  sendCmd("AT+CSSLCFG=\"sslversion\",0,3");                           // TLS 1.2
  sendCmdResp("AT+CSSLCFG=\"CONVERT\",2,\"ca.crt\"", 5000);           // CA LIST（ERRORでも続行）
  sendCmd("AT+CSSLCFG=\"CONVERT\",1,\"client.crt\",\"client.key\"");  // クライアント証明書+鍵

  if (connect())
    syncTime();
}
