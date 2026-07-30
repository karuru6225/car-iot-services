#include "can.h"
#include <Arduino.h>
#include <driver/twai.h>
#include <string.h>
#include "../board_pins.h"

static const uint8_t CAN_RX_PIN = boardPins().gu00Pin; // GPIO4: MCP2562FD RXD 側
static const uint8_t CAN_TX_PIN = boardPins().gu01Pin; // GPIO5: MCP2562FD TXD 側
static const uint8_t CAN_EN_PIN = boardPins().gu0EnPin; // GPIO6: AO3401A ゲート（HIGH=電源ON）

static const uint32_t CAN_REQ_ID = 0x18DB33F1;    // 29-bit functional addressing
static const uint32_t CAN_RESP_MASK = 0x18DAF100; // 応答IDの上位24bit（下位8bit=ECUアドレス）

// バスオフから連続で復帰できない場合にフル再初期化へエスカレーションする閾値
static const uint32_t CAN_FAIL_ESCALATE_THRESHOLD = 20;

static bool s_ready = false;
static uint32_t s_failCount = 0;

bool canInit()
{
  if (s_ready)
    return true;

  pinMode(CAN_EN_PIN, OUTPUT);
  digitalWrite(CAN_EN_PIN, HIGH);
  delay(50);

  twai_general_config_t gConfig = TWAI_GENERAL_CONFIG_DEFAULT(
      (gpio_num_t)CAN_TX_PIN, (gpio_num_t)CAN_RX_PIN, TWAI_MODE_NORMAL);
  twai_timing_config_t tConfig = TWAI_TIMING_CONFIG_500KBITS();
  twai_filter_config_t fConfig = TWAI_FILTER_CONFIG_ACCEPT_ALL();

  if (twai_driver_install(&gConfig, &tConfig, &fConfig) != ESP_OK)
  {
    digitalWrite(CAN_EN_PIN, LOW);
    return false;
  }
  if (twai_start() != ESP_OK)
  {
    twai_driver_uninstall();
    digitalWrite(CAN_EN_PIN, LOW);
    return false;
  }

  s_ready = true;
  s_failCount = 0;
  return true;
}

void canDeinit()
{
  if (s_ready)
  {
    twai_stop();
    twai_driver_uninstall();
    s_ready = false;
  }
  pinMode(CAN_EN_PIN, OUTPUT);
  digitalWrite(CAN_EN_PIN, LOW);
}

// バスオフ状態なら復帰要求のみ行う（ドライバ再インストールは伴わない軽量処理）
static void recoverIfBusOff()
{
  twai_status_info_t sts;
  if (twai_get_status_info(&sts) != ESP_OK)
    return;
  if (sts.state == TWAI_STATE_BUS_OFF)
    twai_initiate_recovery();
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

  bool ok = twai_transmit(&tx, pdMS_TO_TICKS(10)) == ESP_OK;
  if (!ok)
  {
    if (++s_failCount >= CAN_FAIL_ESCALATE_THRESHOLD)
    {
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

bool canReceiveObdResponse(uint8_t *data, uint8_t *dlc, uint32_t timeoutMs)
{
  if (!s_ready)
    return false;

  twai_message_t rx = {};
  unsigned long deadline = millis() + timeoutMs;
  while (millis() < deadline)
  {
    if (twai_receive(&rx, pdMS_TO_TICKS(10)) == ESP_OK)
    {
      bool is29bit = rx.extd && (rx.identifier & 0xFFFFFF00) == CAN_RESP_MASK;
      bool is11bit = !rx.extd && rx.identifier == 0x7E8; // フォールバック
      if (is29bit || is11bit)
      {
        memcpy(data, rx.data, rx.data_length_code);
        *dlc = rx.data_length_code;
        return true;
      }
    }
  }
  return false;
}
