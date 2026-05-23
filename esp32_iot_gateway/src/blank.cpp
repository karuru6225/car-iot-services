#include <Arduino.h>
#include "driver/twai.h"

#define RELAY_0_PIN 11
#define RELAY_1_PIN 13
#define RELAY_2_PIN 15

#define GU0_0 4  // CAN RX (MCP2562FD RXD)
#define GU0_1 5  // CAN TX (MCP2562FD TXD)
#define GU0_EN 6 // AO3401A パワースイッチ: HIGH=電源ON, LOW=電源OFF

// 他ノードなしで単体テストするときは TWAI_MODE_NO_ACK
// 他ノードがいるときは TWAI_MODE_NORMAL
#define CAN_MODE TWAI_MODE_NO_ACK

static unsigned long lastSendTime = 0;
static uint32_t sendCount = 0;
static bool twaiReady = false;

void setup()
{
  Serial.begin(115200);
  delay(1000);
  Serial.println("=== MCP2562FD CAN test ===");

  // AO3401A パワースイッチ HIGH → モジュール電源 ON
  pinMode(GU0_EN, OUTPUT);
  digitalWrite(GU0_EN, HIGH);
  delay(50); // 電源安定待ち

  twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(
      (gpio_num_t)GU0_1,
      (gpio_num_t)GU0_0,
      CAN_MODE);
  // twai_timing_config_t t_config = TWAI_TIMING_CONFIG_500KBITS();
  twai_timing_config_t t_config = TWAI_TIMING_CONFIG_125KBITS();
  twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

  esp_err_t err = twai_driver_install(&g_config, &t_config, &f_config);
  if (err != ESP_OK)
  {
    Serial.printf("TWAI install failed: 0x%x\n", err);
    return;
  }

  err = twai_start();
  if (err != ESP_OK)
  {
    Serial.printf("TWAI start failed: 0x%x\n", err);
    return;
  }

  twaiReady = true;
  // Serial.println("TWAI ready. 500kbps");
  Serial.println("TWAI ready. 125kbps");
}

void loop()
{
  if (!twaiReady)
    return;

  // 受信チェック（ノンブロッキング）
  twai_message_t rx = {};
  if (twai_receive(&rx, 0) == ESP_OK)
  {
    Serial.printf("RX id=0x%03lX len=%d data=", rx.identifier, rx.data_length_code);
    for (int i = 0; i < rx.data_length_code; i++)
    {
      Serial.printf("%02X ", rx.data[i]);
    }
    Serial.println();
  }

  // 1秒ごとに送信
  if (millis() - lastSendTime >= 1000)
  {
    lastSendTime = millis();

    twai_message_t tx = {};
    tx.identifier = 0x123;
    tx.data_length_code = 4;
    tx.data[0] = (sendCount >> 24) & 0xFF;
    tx.data[1] = (sendCount >> 16) & 0xFF;
    tx.data[2] = (sendCount >> 8) & 0xFF;
    tx.data[3] = sendCount & 0xFF;

    esp_err_t err = twai_transmit(&tx, pdMS_TO_TICKS(10));
    if (err == ESP_OK)
    {
      Serial.printf("TX ok  id=0x123 cnt=%lu\n", sendCount);
    }
    else
    {
      Serial.printf("TX fail 0x%x cnt=%lu\n", err, sendCount);
      twai_status_info_t st;
      twai_get_status_info(&st);
      Serial.printf("  state=%d tx_err=%lu rx_err=%lu\n",
                    st.state, st.tx_error_counter, st.rx_error_counter);
    }
    sendCount++;
  }
}
