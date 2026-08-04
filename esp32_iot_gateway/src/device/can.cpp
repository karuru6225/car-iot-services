#include "can.h"
#include <Arduino.h>
#include <driver/twai.h>
#include <string.h>
#include "../board_pins.h"
#include "../service/logger.h"

static const uint8_t CAN_RX_PIN = boardPins().gu01Pin;  // GPIO4: MCP2562FD RXD 側
static const uint8_t CAN_TX_PIN = boardPins().gu00Pin;  // GPIO5: MCP2562FD TXD 側
static const uint8_t CAN_EN_PIN = boardPins().gu0EnPin; // GPIO6: AO3401A ゲート（HIGH=電源ON）

static const uint32_t CAN_REQ_ID = 0x18DB33F1;    // 29-bit functional addressing
static const uint32_t CAN_RESP_MASK = 0x18DAF100; // 応答IDの上位24bit（下位8bit=ECUアドレス）

// バスオフから連続で復帰できない場合にフル再初期化へエスカレーションする閾値
static const uint32_t CAN_FAIL_ESCALATE_THRESHOLD = 20;

static bool s_ready = false;
static uint32_t s_failCount = 0;

// TWAIコントローラの状態・エラーカウンタをまとめてログ出力（診断用）
void canLogStatus(const char *tag)
{
  twai_status_info_t sts;
  if (twai_get_status_info(&sts) != ESP_OK)
  {
    logger.printf("[CAN] %s: twai_get_status_info 失敗\n", tag);
    return;
  }
  const char *stateStr =
      sts.state == TWAI_STATE_RUNNING ? "RUNNING" : sts.state == TWAI_STATE_BUS_OFF ? "BUS_OFF"
                                                 : sts.state == TWAI_STATE_RECOVERING ? "RECOVERING"
                                                                                      : "STOPPED";
  logger.printf("[CAN] %s: state=%s TEC=%u REC=%u txQ=%u rxQ=%u txFail=%u rxMiss=%u rxOverrun=%u arbLost=%u busErr=%u\n",
                tag, stateStr, sts.tx_error_counter, sts.rx_error_counter,
                sts.msgs_to_tx, sts.msgs_to_rx, sts.tx_failed_count,
                sts.rx_missed_count, sts.rx_overrun_count, sts.arb_lost_count, sts.bus_error_count);
}

bool canInit()
{
  if (s_ready)
    return true;

  logger.printf("[CAN] canInit: 開始 TX=GPIO%u RX=GPIO%u EN=GPIO%u\n", CAN_TX_PIN, CAN_RX_PIN, CAN_EN_PIN);

  pinMode(CAN_EN_PIN, OUTPUT);
  digitalWrite(CAN_EN_PIN, HIGH);
  delay(50);

  twai_general_config_t gConfig = TWAI_GENERAL_CONFIG_DEFAULT(
      (gpio_num_t)CAN_TX_PIN, (gpio_num_t)CAN_RX_PIN, TWAI_MODE_NORMAL);
  twai_timing_config_t tConfig = TWAI_TIMING_CONFIG_500KBITS();
  twai_filter_config_t fConfig = TWAI_FILTER_CONFIG_ACCEPT_ALL();

  esp_err_t err = twai_driver_install(&gConfig, &tConfig, &fConfig);
  if (err != ESP_OK)
  {
    logger.printf("[CAN] canInit: twai_driver_install 失敗 (%s)\n", esp_err_to_name(err));
    digitalWrite(CAN_EN_PIN, LOW);
    return false;
  }
  err = twai_start();
  if (err != ESP_OK)
  {
    logger.printf("[CAN] canInit: twai_start 失敗 (%s)\n", esp_err_to_name(err));
    twai_driver_uninstall();
    digitalWrite(CAN_EN_PIN, LOW);
    return false;
  }

  s_ready = true;
  s_failCount = 0;
  logger.println("[CAN] canInit: 起動完了（500kbps NORMAL）");
  canLogStatus("canInit直後");
  return true;
}

void canDeinit()
{
  if (s_ready)
  {
    canLogStatus("canDeinit直前");
    twai_stop();
    twai_driver_uninstall();
    s_ready = false;
    logger.println("[CAN] canDeinit: 停止");
  }
  pinMode(CAN_EN_PIN, OUTPUT);
  digitalWrite(CAN_EN_PIN, LOW);
}

// バスオフ状態なら復帰要求のみ行う（ドライバ再インストールは伴わない軽量処理）
static void recoverIfBusOff()
{
  twai_status_info_t sts;
  if (twai_get_status_info(&sts) != ESP_OK)
  {
    logger.println("[CAN] recoverIfBusOff: twai_get_status_info 失敗");
    return;
  }
  if (sts.state == TWAI_STATE_BUS_OFF)
  {
    canLogStatus("バスオフ検出");
    logger.println("[CAN] バスオフ検出 → 復帰要求");
    esp_err_t err = twai_initiate_recovery();
    if (err != ESP_OK)
      logger.printf("[CAN] twai_initiate_recovery 失敗 (%s)\n", esp_err_to_name(err));
  }
  else if (sts.state != TWAI_STATE_RUNNING)
  {
    logger.printf("[CAN] recoverIfBusOff: 想定外の状態 state=%d\n", (int)sts.state);
  }
}

bool canSendObdRequest(uint8_t pid)
{
  if (!s_ready)
    return false;

  recoverIfBusOff();

  twai_message_t tx = {};
  tx.identifier = CAN_REQ_ID;
  tx.extd = 1;
  tx.data_length_code = 8;
  tx.data[0] = 0x02; // PCI: Single Frame, length=2
  tx.data[1] = 0x01; // Mode 01
  tx.data[2] = pid;
  // data[3..7] = 0x00（ISO 15765-4 パディング）

  esp_err_t txErr = twai_transmit(&tx, pdMS_TO_TICKS(10));
  bool ok = txErr == ESP_OK;
  if (!ok)
  {
    logger.printf("[CAN] canSendObdRequest: 送信失敗 pid=0x%02X err=%s (連続%u回)\n",
                  pid, esp_err_to_name(txErr), s_failCount + 1);
    if (++s_failCount >= CAN_FAIL_ESCALATE_THRESHOLD)
    {
      logger.printf("[CAN] 連続送信失敗%u回 → フル再初期化\n", s_failCount);
      canLogStatus("再初期化前");
      canDeinit();
      canInit();
    }
  }
  else
  {
    s_failCount = 0;
  }
  return ok;
}

// ECUからのOBD応答フレームか判定する（29bit: 0x18DAF1xx、11bit フォールバック: 0x7E8）
static bool isObdResponseFrame(const twai_message_t &rx)
{
  bool is29bit = rx.extd && (rx.identifier & 0xFFFFFF00) == CAN_RESP_MASK;
  bool is11bit = !rx.extd && rx.identifier == 0x7E8; // フォールバック
  return is29bit || is11bit;
}

bool canReceiveObdResponse(uint8_t *data, uint8_t *dlc, uint32_t timeoutMs)
{
  if (!s_ready)
    return false;

  twai_message_t rx = {};
  unsigned long deadline = millis() + timeoutMs;
  uint32_t unmatchedCount = 0;
  while (millis() < deadline)
  {
    if (twai_receive(&rx, pdMS_TO_TICKS(10)) == ESP_OK)
    {
      if (isObdResponseFrame(rx))
      {
        // ISO-TP PCIバイト（1バイト目）を剥がし、ペイロード（41 PID data...）のみを返す
        memcpy(data, rx.data + 1, rx.data_length_code - 1);
        *dlc = rx.data_length_code - 1;
        return true;
      }
      // 想定外ID（別ECU応答やバス上の他フレーム）を受信 → 診断用に記録
      unmatchedCount++;
      logger.printf("[CAN] 未一致フレーム受信 id=0x%08X extd=%d dlc=%u\n",
                    (unsigned)rx.identifier, rx.extd, rx.data_length_code);
    }
  }
  if (unmatchedCount > 0)
    logger.printf("[CAN] canReceiveObdResponse: タイムアウト（未一致%u件受信、一致なし）\n", unmatchedCount);
  return false;
}
