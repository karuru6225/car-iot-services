#pragma once
#include <stdint.h>

#ifndef GIT_HASH
#define GIT_HASH "00000000"
#endif
#define FIRMWARE_VERSION "1.0.0+" GIT_HASH

static const uint16_t SWITCHBOT_COMPANY_ID = 0x0969;
static const int SCAN_TIME = 10; // BLE スキャン時間（秒）
static const int QUEUE_SIZE = 20;
static const int MAX_TARGETS = 10;
static const int MAX_FOUND = 20;
static const uint8_t BTN_PIN = 41; // M5Atom S3 ボタン (Active-LOW)
static const uint32_t LONG_PRESS_MS = 1000;
static const uint32_t DEBOUNCE_MS = 50;
static const uint32_t SLEEP_INTERVAL_SEC  = 300; // Deep Sleep 間隔（秒）
static const int      PAYLOAD_BATTERY_SIZE = 96;  // battery ペイロードバッファ
static const int      PAYLOAD_SENSOR_SIZE  = 256; // sensor ペイロードバッファ（mf hex 込み）
