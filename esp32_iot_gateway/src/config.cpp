#include "config.h"
#include <esp_mac.h>
#include <nvs.h>
#include <cstdio>

static constexpr char NVS_NS_DEVICE[]  = "device";
static constexpr char NVS_NS_LTE[]    = "lte";
static constexpr char NVS_NS_OTA[]    = "ota";
static constexpr char NVS_MQTT_HOST[] = "mqtt_host";
static constexpr char NVS_CERT_CRC[]  = "cert_crc";
static constexpr char NVS_JOB_ID[]    = "job_id";

const char *getDeviceId()
{
  static char id[32] = {};
  if (id[0] != '\0') return id;

  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  snprintf(id, sizeof(id), "esp32-gw-%02x%02x%02x%02x%02x%02x",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return id;
}

const char *getMqttHost()
{
  static char host[128] = {};
  if (host[0] != '\0') return host;

  nvs_handle_t nvs;
  size_t len = sizeof(host);
  if (nvs_open(NVS_NS_DEVICE, NVS_READONLY, &nvs) == ESP_OK) {
    nvs_get_str(nvs, NVS_MQTT_HOST, host, &len);
    nvs_close(nvs);
  }
  return host[0] != '\0' ? host : nullptr;
}

bool getCertCrc(uint32_t &out)
{
  nvs_handle_t nvs;
  if (nvs_open(NVS_NS_LTE, NVS_READONLY, &nvs) != ESP_OK) return false;
  bool ok = nvs_get_u32(nvs, NVS_CERT_CRC, &out) == ESP_OK;
  nvs_close(nvs);
  return ok;
}

void setCertCrc(uint32_t crc)
{
  nvs_handle_t nvs;
  if (nvs_open(NVS_NS_LTE, NVS_READWRITE, &nvs) != ESP_OK) return;
  nvs_set_u32(nvs, NVS_CERT_CRC, crc);
  nvs_commit(nvs);
  nvs_close(nvs);
}

bool getPendingJobId(char *buf, size_t len)
{
  nvs_handle_t nvs;
  if (nvs_open(NVS_NS_OTA, NVS_READONLY, &nvs) != ESP_OK) return false;
  bool ok = nvs_get_str(nvs, NVS_JOB_ID, buf, &len) == ESP_OK && buf[0] != '\0';
  nvs_close(nvs);
  return ok;
}

void setPendingJobId(const char *jobId)
{
  nvs_handle_t nvs;
  if (nvs_open(NVS_NS_OTA, NVS_READWRITE, &nvs) != ESP_OK) return;
  nvs_set_str(nvs, NVS_JOB_ID, jobId);
  nvs_commit(nvs);
  nvs_close(nvs);
}

void clearPendingJobId()
{
  nvs_handle_t nvs;
  if (nvs_open(NVS_NS_OTA, NVS_READWRITE, &nvs) != ESP_OK) return;
  nvs_erase_key(nvs, NVS_JOB_ID);
  nvs_commit(nvs);
  nvs_close(nvs);
}
