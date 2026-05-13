#pragma once
#include <stdint.h>
#include <stddef.h>

#ifndef GIT_HASH
#define GIT_HASH "00000000"
#endif
#define FIRMWARE_VERSION "1.5.1+" GIT_HASH

// 動作モード
enum class OperationMode { DEEP_SLEEP, CONTINUOUS };

// リレー動作モード
enum class RelayMode { SLEEP_INDICATOR, RELAY_OFF };

// DeepSleep
static const uint32_t SLEEP_INTERVAL_SEC = 300;

// BLE
static const uint16_t SWITCHBOT_COMPANY_ID = 0x0969;
static const int SCAN_TIME = 10;
static const int QUEUE_SIZE = 20;
static const int MAX_TARGETS = 10;
static const int PAYLOAD_SENSOR_SIZE = 256;

// SPIFFS 証明書パス
#define CERT_PATH_CA "/certs/ca.crt"
#define CERT_PATH_DEVICE "/certs/device.crt"
#define CERT_PATH_KEY "/certs/device.key"

// オフラインバッファ（RTC メモリ + SPIFFS 永続化）
// 1エントリ ≈ 28 bytes、RTC SLOW 8KB に収まる上限
static const int OFFLINE_BUFFER_MAX = 200;
#define OFFLINE_BUFFER_PATH "/buffer.bin"

// MQTT デフォルト設定
static const int MQTT_PORT = 8883;

// Wi-Fi STA MAC アドレスから "esp32-gw-aabbccddeeff" 形式の device ID を返す
const char *getDeviceId();

// NVS から MQTT ホストを返す。未設定の場合は nullptr
const char *getMqttHost();

// 証明書 CRC の取得・保存（lte 用）
bool getCertCrc(uint32_t &out);
void setCertCrc(uint32_t crc);

// OTA ジョブ ID の取得・保存・削除（ota 用）
bool getPendingJobId(char *buf, size_t len);
void setPendingJobId(const char *jobId);
void clearPendingJobId();

// リレー動作モードの取得・保存（device 用、デフォルト: SLEEP_INDICATOR）
RelayMode getRelayMode();
void setRelayMode(RelayMode mode);

// Ah オフセットの取得・保存（battery 用、デフォルト: 0）
int32_t getAhOffset();
void setAhOffset(int32_t ah);

// メニュー操作で消去するデータを一括クリア（"device" ネームスペースは保持）
void clearMenuData();
