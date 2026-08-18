#pragma once
#include <stdint.h>
#include <stddef.h>

// デバッグ用: 有効にするとLTE/MQTT等のネットワーク処理を丸ごとスキップする。
// main.cpp単体のソース内#defineだと他の翻訳単位（service/mode_*.cpp等）に伝播しないため、
// 全層から参照可能なここで定義する（コメントアウトがデフォルト）
// #define DEBUG_SKIP_NETWORK

#ifndef GIT_HASH
#define GIT_HASH "00000000"
#endif

// FIRMWARE_VERSIONはソースが正。MAJORは基板シリーズ固定（1=v1基板、2=v2基板）。
// リリース時はここを更新してからタグを打つ（タグはCI発火とGitHub Release表示用、
// ここの値と一致しないとfirmware-release.ymlが検証エラーで停止する。RELEASE.md参照）
#if BOARD_VERSION == 1
#define FIRMWARE_VERSION_BASE "1.23.0" // FIRMWARE_VERSION_V1
#elif BOARD_VERSION == 2
#define FIRMWARE_VERSION_BASE "2.1.0" // FIRMWARE_VERSION_V2
#else
#error "BOARD_VERSION must be 1 or 2"
#endif

#define FIRMWARE_VERSION FIRMWARE_VERSION_BASE "+" GIT_HASH

// 動作モード
enum class OperationMode
{
  DEEP_SLEEP,
  CONTINUOUS,       // measure/publish + OBD-II(CAN)ポーリングを1秒間隔で実行
  TIMED_CONTINUOUS, // Shadow override_next_mode から指定。指定分数が経過するまで CONTINUOUS を繰り返し、期限到達で自動 DEEP_SLEEP
};

// DeepSleep
static const uint32_t SLEEP_INTERVAL_SEC = 300;
// DeepSleep突入前にBLE接続を待つ最低時間（スマホ側の接続リトライと合わせる。OBD.md参照）
static const uint32_t BLE_WAKE_WINDOW_SEC = 15;

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

// NVS から基板バージョンを返す（provisioning時に書き込み）。未設定の場合は1を返す
// 現時点ではboardPins()の選択には使わず、取得のみ可能
uint8_t getBoardVersion();

// 証明書 CRC の取得・保存（lte 用）
bool getCertCrc(uint32_t &out);
void setCertCrc(uint32_t crc);

// OTA ジョブ ID の取得・保存・削除（ota 用）
bool getPendingJobId(char *buf, size_t len);
void setPendingJobId(const char *jobId);
void clearPendingJobId();

// Ah オフセットの取得・保存（battery 用、デフォルト: 0）
int32_t getAhOffset();
void setAhOffset(int32_t ah);

// 充電タイムアウトの取得・保存（battery 用、デフォルト: 20 分）
uint32_t getChgTimeoutMin();
void setChgTimeoutMin(uint32_t minutes);

// 充電開始・停止電圧しきい値の取得・保存（battery 用）
// 開始: デフォルト 11.7V、停止: デフォルト 12.5V
float getChgStartV();
void setChgStartV(float v);
float getChgStopV();
void setChgStopV(float v);

// sub-main 電圧差しきい値の取得・保存（battery 用、デフォルト: 0.3V）
// diff < minDiffV なら充電開始しない・充電中なら停止する
float getChgMinDiffV();
void setChgMinDiffV(float v);

// 充電フラグ（RTC メモリ。charge_start/charge_stop コマンドで切り替え）
bool isCharging();
void setCharging(bool v);

// デバッグログ有効フラグ（NVS, デフォルト: false）
bool getDebugLogEnabled();
void setDebugLogEnabled(bool enabled);

// メニュー操作で消去するデータを一括クリア（"device" ネームスペースは保持）
void clearMenuData();
