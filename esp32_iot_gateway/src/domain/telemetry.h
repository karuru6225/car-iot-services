#pragma once
#include <stddef.h>
#include <time.h>
#include "measurement.h"
#include "thermometer.h"
#include "co2meter.h"

// バッテリーテレメトリを sensors/{id}/data 向けに組み立てる
int buildBatteryPayload(char *buf, size_t size,
                        const VoltageReading &main,
                        const VoltageReading &sub,
                        const PowerReading &pwr,
                        time_t ts);

// Shadow reported 向けデバイス設定ペイロードを組み立てる
// clearDesired=true のとき "desired":null を付加して desired をクリアする
int buildConfigPayload(char *buf, size_t size, bool clearDesired = false);

// BLE センサーペイロードを buf に書き込む（tsField は ",\"ts\":\"...\"" 形式または空文字列）
int buildThermometerPayload(char *buf, size_t size,
                             const ThermometerData &d,
                             const char *tsField);

int buildCo2Payload(char *buf, size_t size,
                    const Co2MeterData &d,
                    const char *tsField);
