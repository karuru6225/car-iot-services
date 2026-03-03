#pragma once
#include <stdint.h>

static const uint16_t SWITCHBOT_COMPANY_ID = 0x0969;
static const int      SCAN_TIME     = 5;
static const int      QUEUE_SIZE    = 20;
static const int      MAX_TARGETS   = 10;
static const int      MAX_FOUND     = 20;
static const uint8_t  BTN_PIN       = 41;     // M5Atom S3 ボタン (Active-LOW)
static const uint32_t LONG_PRESS_MS      = 1000;
static const uint32_t DEBOUNCE_MS        = 50;
static const uint32_t SLEEP_INTERVAL_SEC = 300; // Deep Sleep 間隔（秒）
