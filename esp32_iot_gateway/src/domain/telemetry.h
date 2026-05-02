#pragma once
#include <stddef.h>
#include <time.h>
#include "measurement.h"

// AWS IoT Shadow の reported ペイロードを buf に書き込む
// 戻り値: 書き込んだバイト数（snprintf と同じ）
int buildShadowPayload(char *buf, size_t size,
                       const VoltageReading &v1,
                       const VoltageReading &v2,
                       const PowerReading &pwr,
                       time_t ts);
