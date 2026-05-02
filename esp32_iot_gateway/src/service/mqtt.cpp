#include "mqtt.h"
#include "../device/lte.h"
#include "logger.h"
#include "../config.h"

Mqtt mqtt;

bool Mqtt::isConnected()
{
  return lte.sendCmdResp("AT+SMSTATE?", 3000).indexOf("+SMSTATE: 1") >= 0;
}

bool Mqtt::connect()
{
  if (isConnected())
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
  if (!lte.sendCmd(urlCmd.c_str()))
    return false;

  String idCmd = "AT+SMCONF=\"CLIENTID\",\"";
  idCmd += getDeviceId();
  idCmd += "\"";
  lte.sendCmd(idCmd.c_str());
  lte.sendCmd("AT+SMCONF=\"KEEPTIME\",60");
  lte.sendCmd("AT+SMCONF=\"CLEANSS\",1");
  lte.sendCmd("AT+SMSSL=1,ca.crt,client.crt");
  lte.sendCmd("AT+SMDISC", 3000);

  logger.println("[MQTT] 接続中...");
  if (!lte.sendCmd("AT+SMCONN", 30000))
  {
    logger.println("[MQTT] 接続失敗");
    return false;
  }
  logger.println("[MQTT] 接続完了");
  return true;
}

bool Mqtt::publish(const char *topic, const char *payload)
{
  if (!connect())
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

bool Mqtt::subscribe(const char *topic)
{
  if (!connect())
    return false;

  String cmd = "AT+SMSUB=\"";
  cmd += topic;
  cmd += "\",0";
  bool ok = lte.sendCmd(cmd.c_str());
  logger.printf("[MQTT] subscribe %s: %s\n", topic, ok ? "OK" : "NG");
  return ok;
}

bool Mqtt::pollMqtt(char *outTopic, int topicSize,
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

    // SIM7080G URC: +SMSUB: "<topic>","<payload>"\r\n
    // \n が来るまでバッファが揃うのを待つ
    int nl = buf.indexOf('\n', idx);
    if (nl < 0)
    {
      delay(10);
      continue;
    }

    // トピック: 引用符で囲まれた部分
    int q1 = buf.indexOf('"', idx + 7);
    int q2 = buf.indexOf('"', q1 + 1);
    if (q1 < 0 || q2 < 0) { buf = buf.substring(nl + 1); continue; }

    // ペイロード: q2+1 は ","  の ","、q2+3 から中身、末尾の " まで
    int payloadStart = q2 + 3;
    int endQuote     = buf.lastIndexOf('"', nl);
    if (payloadStart >= nl || endQuote <= q2 + 2) { buf = buf.substring(nl + 1); continue; }
    int payloadLen = endQuote - payloadStart;

    if (outTopic && topicSize > 0)
    {
      int len = min(q2 - q1 - 1, topicSize - 1);
      strncpy(outTopic, buf.c_str() + q1 + 1, len);
      outTopic[len] = '\0';
    }
    if (outPayload && payloadSize > 0)
    {
      int len = min(payloadLen, payloadSize - 1);
      strncpy(outPayload, buf.c_str() + payloadStart, len);
      outPayload[len] = '\0';
    }

    logger.printf("[MQTT] recv(%d bytes): %.100s\n", payloadLen, buf.c_str() + payloadStart);
    return true;
  }
  return false;
}
