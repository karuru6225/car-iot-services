#pragma once
#include <stddef.h>
#include <time.h>
#include "measurement.h"
#include "thermometer.h"
#include "co2meter.h"

// AWS IoT Shadow の reported ペイロードを buf に書き込む
// 戻り値: 書き込んだバイト数（snprintf と同じ）
int buildShadowPayload(char *buf, size_t size,
                       const VoltageReading &v1,
                       const VoltageReading &v2,
                       const PowerReading &pwr,
                       time_t ts);

// BLE センサーペイロードを buf に書き込む（tsField は ",\"ts\":\"...\"" 形式または空文字列）
int buildThermometerPayload(char *buf, size_t size,
                             const ThermometerData &d,
                             const char *tsField);

int buildCo2Payload(char *buf, size_t size,
                    const Co2MeterData &d,
                    const char *tsField);
