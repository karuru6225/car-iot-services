#pragma once
#include <stdint.h>

#ifndef GIT_HASH
#define GIT_HASH "00000000"
#endif
#define FIRMWARE_VERSION "1.0.0+" GIT_HASH

// DeepSleep
static const uint32_t SLEEP_INTERVAL_SEC = 300;

// SPIFFS 証明書パス
#define CERT_PATH_CA     "/certs/ca.crt"
#define CERT_PATH_DEVICE "/certs/device.crt"
#define CERT_PATH_KEY    "/certs/device.key"

// MQTT デフォルト設定
static const int MQTT_PORT = 8883;
