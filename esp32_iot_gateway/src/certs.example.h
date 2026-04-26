#pragma once
#include <pgmspace.h>   // PROGMEM の定義（ESP32 Arduino 必須）
// AWS IoT Core 接続用証明書
// infra/gen_certs.ps1 で再生成すること（terraform apply 後）

#define MQTT_HOST      "xxxxxxxxxxxxx-ats.iot.ap-northeast-1.amazonaws.com"
#define MQTT_PORT      8883
#define MQTT_CLIENT_ID "esp32-gateway-001"
#define MQTT_TOPIC_DATA "sensors/" MQTT_CLIENT_ID "/data"
#define MQTT_TOPIC_OTA  "sensors/" MQTT_CLIENT_ID "/ota"

static const char AWS_ROOT_CA[] PROGMEM = R"EOF(-----BEGIN CERTIFICATE-----
MIIDQTCCAimgAwIBAgITBmyfz5m/jAo54vB4ikPmljZbyjANBgkqhkiG9w0BAQsF
...
MVLe6A==
-----END CERTIFICATE-----)EOF";

static const char DEVICE_CERT[] PROGMEM = R"EOF(-----BEGIN CERTIFICATE-----
...
-----END CERTIFICATE-----
)EOF";

static const char DEVICE_KEY[] PROGMEM = R"EOF(-----BEGIN RSA PRIVATE KEY-----
...
-----END RSA PRIVATE KEY-----
)EOF";
