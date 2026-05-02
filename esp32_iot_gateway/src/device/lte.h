#pragma once
#include <Arduino.h>
#include <functional>
#include <time.h>

#define TINY_GSM_MODEM_SIM7080
#define TINY_GSM_RX_BUFFER 1024
#define SerialAT Serial2
#include <TinyGsmClient.h>

static const uint8_t LTE_RX_PIN = 7;
static const uint8_t LTE_TX_PIN = 8;
static const uint8_t LTE_EN_PIN = 9;
static const char *APN = "soracom.io";
static const char *APN_USER = "sora";
static const char *APN_PASS = "sora";
static const int SEND_INTERVAL_SEC = 60;

class Lte
{
public:
  // モデム初期化・APN 設定・GPRS 接続・時刻同期・証明書設定
  void setup();

  // ネットワーク登録 + GPRS 接続（再接続にも使用）
  bool connect();

  // GPRS + MQTT 切断 + LTE 電源オフ
  void disconnect();

  // ラジオオフ（DeepSleep 前に呼ぶ）
  void radioOff();

  // 電源オフ（完全に電源を切る。再度電源オンするには setup() を呼ぶ必要がある）
  void powerOff();

  // GPRS 接続中か確認
  bool isConnected();

  // AT+CCLK? で ESP32 RTC を UTC に同期
  bool syncTime();

  // AT コマンド送受信（service/mqtt, service/https が使用）
  String sendCmdResp(const char *cmd, uint32_t timeoutMs = 3000);
  bool sendCmd(const char *cmd, uint32_t timeoutMs = 3000);

  bool readFile(const char *filename, std::function<bool(const uint8_t *, size_t)> onChunk);

private:
  TinyGsm _modem{SerialAT};
  TinyGsmClient _client{_modem};

  void uploadCert(const char *filename, const char *pem);
};

extern Lte lte;
