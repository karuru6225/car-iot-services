#pragma once
#include <stdint.h>
#include <stddef.h>

// Wi-Fi STA MAC アドレスから "esp32-gw-aabbccddeeff" 形式の device ID を返す
const char *getDeviceId();

// NVS から MQTT ホストを返す。未設定の場合は nullptr
const char *getMqttHost();

// 証明書 CRC の取得・保存（lte 用）
bool     getCertCrc(uint32_t &out);
void     setCertCrc(uint32_t crc);

// OTA ジョブ ID の取得・保存・削除（ota 用）
bool     getPendingJobId(char *buf, size_t len);
void     setPendingJobId(const char *jobId);
void     clearPendingJobId();
