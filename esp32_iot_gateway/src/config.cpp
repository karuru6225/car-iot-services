#include "config.h"
#include <esp_mac.h>
#include <nvs.h>
#include <cstdio>
#include <cstring>

static constexpr char NVS_NS_DEVICE[] = "device";
static constexpr char NVS_NS_LTE[] = "lte";
static constexpr char NVS_NS_OTA[] = "ota";
static constexpr char NVS_NS_BATTERY[] = "battery";
static constexpr char NVS_MQTT_HOST[] = "mqtt_host";
static constexpr char NVS_CERT_CRC[] = "cert_crc";
static constexpr char NVS_JOB_ID[] = "job_id";
static constexpr char NVS_RELAY_MODE[] = "relay_mode";
static constexpr char NVS_DEBUG_LOG[] = "debug_log";

const char *getDeviceId()
{
  static char id[32] = {};
  if (id[0] != '\0')
    return id;

  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  snprintf(id, sizeof(id), "esp32-gw-%02x%02x%02x%02x%02x%02x",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return id;
}

const char *getMqttHost()
{
  static char host[128] = {};
  if (host[0] != '\0')
    return host;

  nvs_handle_t nvs;
  size_t len = sizeof(host);
  if (nvs_open(NVS_NS_DEVICE, NVS_READONLY, &nvs) == ESP_OK)
  {
    nvs_get_str(nvs, NVS_MQTT_HOST, host, &len);
    nvs_close(nvs);
  }
  return host[0] != '\0' ? host : nullptr;
}

bool getCertCrc(uint32_t &out)
{
  nvs_handle_t nvs;
  if (nvs_open(NVS_NS_LTE, NVS_READONLY, &nvs) != ESP_OK)
    return false;
  bool ok = nvs_get_u32(nvs, NVS_CERT_CRC, &out) == ESP_OK;
  nvs_close(nvs);
  return ok;
}

void setCertCrc(uint32_t crc)
{
  nvs_handle_t nvs;
  if (nvs_open(NVS_NS_LTE, NVS_READWRITE, &nvs) != ESP_OK)
    return;
  nvs_set_u32(nvs, NVS_CERT_CRC, crc);
  nvs_commit(nvs);
  nvs_close(nvs);
}

bool getPendingJobId(char *buf, size_t len)
{
  nvs_handle_t nvs;
  if (nvs_open(NVS_NS_OTA, NVS_READONLY, &nvs) != ESP_OK)
    return false;
  bool ok = nvs_get_str(nvs, NVS_JOB_ID, buf, &len) == ESP_OK && buf[0] != '\0';
  nvs_close(nvs);
  return ok;
}

void setPendingJobId(const char *jobId)
{
  nvs_handle_t nvs;
  if (nvs_open(NVS_NS_OTA, NVS_READWRITE, &nvs) != ESP_OK)
    return;
  nvs_set_str(nvs, NVS_JOB_ID, jobId);
  nvs_commit(nvs);
  nvs_close(nvs);
}

void clearPendingJobId()
{
  nvs_handle_t nvs;
  if (nvs_open(NVS_NS_OTA, NVS_READWRITE, &nvs) != ESP_OK)
    return;
  nvs_erase_key(nvs, NVS_JOB_ID);
  nvs_commit(nvs);
  nvs_close(nvs);
}

RelayMode getRelayMode()
{
  nvs_handle_t nvs;
  uint8_t val = (uint8_t)RelayMode::SLEEP_INDICATOR; // デフォルト
  if (nvs_open(NVS_NS_DEVICE, NVS_READONLY, &nvs) == ESP_OK)
  {
    nvs_get_u8(nvs, NVS_RELAY_MODE, &val);
    nvs_close(nvs);
  }
  return (RelayMode)val;
}

void setRelayMode(RelayMode mode)
{
  nvs_handle_t nvs;
  if (nvs_open(NVS_NS_DEVICE, NVS_READWRITE, &nvs) != ESP_OK)
    return;
  nvs_set_u8(nvs, NVS_RELAY_MODE, (uint8_t)mode);
  nvs_commit(nvs);
  nvs_close(nvs);
}

int32_t getAhOffset()
{
  nvs_handle_t nvs;
  int32_t val = 0;
  if (nvs_open(NVS_NS_BATTERY, NVS_READONLY, &nvs) == ESP_OK)
  {
    nvs_get_i32(nvs, "ah_offset", &val);
    nvs_close(nvs);
  }
  return val;
}

void setAhOffset(int32_t ah)
{
  nvs_handle_t nvs;
  if (nvs_open(NVS_NS_BATTERY, NVS_READWRITE, &nvs) != ESP_OK)
    return;
  nvs_set_i32(nvs, "ah_offset", ah);
  nvs_commit(nvs);
  nvs_close(nvs);
}

uint32_t getChgTimeoutMin()
{
  nvs_handle_t nvs;
  uint32_t val = 20;
  if (nvs_open(NVS_NS_BATTERY, NVS_READONLY, &nvs) == ESP_OK)
  {
    nvs_get_u32(nvs, "chg_timeout", &val);
    nvs_close(nvs);
  }
  return val;
}

void setChgTimeoutMin(uint32_t minutes)
{
  nvs_handle_t nvs;
  if (nvs_open(NVS_NS_BATTERY, NVS_READWRITE, &nvs) != ESP_OK)
    return;
  nvs_set_u32(nvs, "chg_timeout", minutes);
  nvs_commit(nvs);
  nvs_close(nvs);
}

bool getDebugLogEnabled()
{
  nvs_handle_t nvs;
  uint8_t val = 0;
  if (nvs_open(NVS_NS_DEVICE, NVS_READONLY, &nvs) == ESP_OK)
  {
    nvs_get_u8(nvs, NVS_DEBUG_LOG, &val);
    nvs_close(nvs);
  }
  return val != 0;
}

void setDebugLogEnabled(bool enabled)
{
  nvs_handle_t nvs;
  if (nvs_open(NVS_NS_DEVICE, NVS_READWRITE, &nvs) != ESP_OK)
    return;
  nvs_set_u8(nvs, NVS_DEBUG_LOG, enabled ? 1 : 0);

  nvs_commit(nvs);
  nvs_close(nvs);
}

// float を uint32_t ビット列として NVS に保存するヘルパー
static float nvsGetFloat(const char *key, float defaultVal)
{
  nvs_handle_t nvs;
  if (nvs_open(NVS_NS_BATTERY, NVS_READONLY, &nvs) != ESP_OK)
    return defaultVal;
  uint32_t bits;
  bool ok = nvs_get_u32(nvs, key, &bits) == ESP_OK;
  nvs_close(nvs);
  if (!ok)
    return defaultVal;
  float val;
  memcpy(&val, &bits, sizeof(float));
  return val;
}

static void nvsSetFloat(const char *key, float val)
{
  nvs_handle_t nvs;
  if (nvs_open(NVS_NS_BATTERY, NVS_READWRITE, &nvs) != ESP_OK)
    return;
  uint32_t bits;
  memcpy(&bits, &val, sizeof(float));
  nvs_set_u32(nvs, key, bits);
  nvs_commit(nvs);
  nvs_close(nvs);
}

float getChgStartV() { return nvsGetFloat("chg_start_v", 11.7f); }
void setChgStartV(float v) { nvsSetFloat("chg_start_v", v); }
float getChgStopV() { return nvsGetFloat("chg_stop_v", 12.5f); }
void setChgStopV(float v) { nvsSetFloat("chg_stop_v", v); }

uint32_t getChgDurationSec()
{
  nvs_handle_t nvs;
  uint32_t val = 1800; // デフォルト 30 分
  if (nvs_open(NVS_NS_BATTERY, NVS_READONLY, &nvs) == ESP_OK)
  {
    nvs_get_u32(nvs, "chg_duration", &val);
    nvs_close(nvs);
  }
  return val;
}

void setChgDurationSec(uint32_t sec)
{
  nvs_handle_t nvs;
  if (nvs_open(NVS_NS_BATTERY, NVS_READWRITE, &nvs) != ESP_OK)
    return;
  nvs_set_u32(nvs, "chg_duration", sec);
  nvs_commit(nvs);
  nvs_close(nvs);
}

static void eraseNvsNamespace(const char *ns)
{
  nvs_handle_t h;
  if (nvs_open(ns, NVS_READWRITE, &h) == ESP_OK)
  {
    nvs_erase_all(h);
    nvs_commit(h);
    nvs_close(h);
  }
}

void clearMenuData()
{
  eraseNvsNamespace(NVS_NS_LTE);
  eraseNvsNamespace(NVS_NS_OTA);
  eraseNvsNamespace("switchbot");
}
