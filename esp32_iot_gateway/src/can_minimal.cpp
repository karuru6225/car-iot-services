#include <Arduino.h>
#include "driver/twai.h"

// GPIO アサイン（新基板）
static const uint8_t CAN_TX_PIN = 7;
static const uint8_t CAN_RX_PIN = 8;
static const uint8_t CAN_EN_PIN = 9;

// 125kbps: DS203 で波形観察しやすい速度（1bit = 8μs）
// 500kbps に変えたい場合は TWAI_TIMING_CONFIG_500KBITS() に変更する
#define CAN_TIMING TWAI_TIMING_CONFIG_125KBITS()

void setup()
{
  Serial.begin(115200);
  delay(1000);
  Serial.println("=== CAN minimal TX test ===");
  Serial.printf("TX=GPIO%d  RX=GPIO%d  EN=GPIO%d\n", CAN_TX_PIN, CAN_RX_PIN, CAN_EN_PIN);

  // MCP2562FD 電源 ON
  pinMode(CAN_EN_PIN, OUTPUT);
  digitalWrite(CAN_EN_PIN, HIGH);
  delay(50);
  Serial.println("CAN_EN HIGH (MCP2562FD powered)");

  // TWAI 初期化（NO_ACK: 単体で送信可能）
  twai_general_config_t gConfig = TWAI_GENERAL_CONFIG_DEFAULT(
      (gpio_num_t)CAN_TX_PIN, (gpio_num_t)CAN_RX_PIN, TWAI_MODE_NO_ACK);
  twai_timing_config_t tConfig = CAN_TIMING;
  twai_filter_config_t fConfig = TWAI_FILTER_CONFIG_ACCEPT_ALL();

  if (twai_driver_install(&gConfig, &tConfig, &fConfig) != ESP_OK) {
    Serial.println("TWAI install FAILED");
    return;
  }
  if (twai_start() != ESP_OK) {
    Serial.println("TWAI start FAILED");
    twai_driver_uninstall();
    return;
  }
  Serial.println("TWAI ready. 125kbps NO_ACK");
  Serial.println("Connect CANH/CANL to oscilloscope and observe frames.");
}

void loop()
{
  static uint32_t cnt = 0;
  static unsigned long lastTxMs = 0;

  if (millis() - lastTxMs >= 1000) {
    lastTxMs = millis();

    twai_message_t tx = {};
    tx.identifier = 0x123;
    tx.data_length_code = 4;
    tx.data[0] = (cnt >> 24) & 0xFF;
    tx.data[1] = (cnt >> 16) & 0xFF;
    tx.data[2] = (cnt >>  8) & 0xFF;
    tx.data[3] =  cnt        & 0xFF;

    esp_err_t err = twai_transmit(&tx, pdMS_TO_TICKS(50));
    if (err == ESP_OK) {
      Serial.printf("TX ok  id=0x%03X cnt=%lu\n", tx.identifier, cnt);
    } else {
      twai_status_info_t sts;
      twai_get_status_info(&sts);
      Serial.printf("TX fail cnt=%lu err=0x%x state=%d txerr=%lu bus_err=%lu\n",
                    cnt, err, (int)sts.state, sts.tx_error_counter, sts.bus_error_count);
    }
    cnt++;
  }
}
