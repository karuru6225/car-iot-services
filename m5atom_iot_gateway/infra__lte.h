#pragma once
#include <Arduino.h>
#include <time.h>

// TinyGSM の define は include より前に必要
#define TINY_GSM_MODEM_SIM7080
#define TINY_GSM_RX_BUFFER 1024
#define SerialAT Serial2
#include <TinyGsmClient.h>

static const uint8_t LTE_RX_PIN        = 6;    // G1 ← U128 TXD
static const uint8_t LTE_TX_PIN        = 5;    // G2 → U128 RXD
static const char *  APN               = "soracom.io";
static const char *  APN_USER          = "sora";
static const char *  APN_PASS          = "sora";
static const int     SEND_INTERVAL_SEC = 60;   // 送信間隔（秒）

class Lte
{
public:
  // モデム初期化・APN 設定・GPRS 接続・時刻同期・証明書設定を行う
  void setup();

  // ネットワーク登録 + GPRS 接続（再接続にも使用）
  bool connect();

  // GPRS + MQTT 切断
  void disconnect();

  // GPRS 接続中か確認する
  bool isConnected();

  // AWS IoT Core に MQTT over TLS で JSON を publish する
  // SIM7080G 内蔵 MQTT (AT+SMCONN/AT+SMPUB) を使用
  bool publish(const char *topic, const char *payload);

  // AT+CCLK? で ESP32 RTC を UTC に同期する
  bool syncTime();

private:
  TinyGsm       _modem{SerialAT};
  TinyGsmClient _client{_modem};

  // 証明書ファイルをモデムのファイルシステムに書き込む
  void uploadCert(const char *filename, const char *pem);

  // MQTT 接続（接続済みの場合はスキップ）
  bool mqttConnect();

  String sendCmdResp(const char *cmd, uint32_t timeoutMs = 3000);
  bool   sendCmd(const char *cmd, uint32_t timeoutMs = 3000);
};

extern Lte lte;
