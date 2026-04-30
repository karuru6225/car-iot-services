#include <Arduino.h>
#include <SPIFFS.h>
#include <Preferences.h>

// provision_device.ps1 で生成される。リポジトリには含めない
#include "provision_config.h"

// Amazon Root CA 1 (RSA 2048) - 固定値（2038年まで有効）
static const char AMAZON_ROOT_CA[] = R"EOF(-----BEGIN CERTIFICATE-----
MIIDQTCCAimgAwIBAgITBmyfz5m/jAo54vB4ikPmljZbyjANBgkqhkiG9w0BAQsF
ADA5MQswCQYDVQQGEwJVUzEPMA0GA1UEChMGQW1hem9uMRkwFwYDVQQDExBBbWF6
b24gUm9vdCBDQSAxMB4XDTE1MDUyNjAwMDAwMFoXDTM4MDExNzAwMDAwMFowOTEL
MAkGA1UEBhMCVVMxDzANBgNVBAoTBkFtYXpvbjEZMBcGA1UEAxMQQW1hem9uIFJv
b3QgQ0EgMTCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBALJ4gHHKeNXj
ca9HgFB0fW7Y14h29Jlo91ghYPl0hAEvrAIthtOgQ3pOsqTQNroBvo3bSMgHFzZM
9O6II8c+6zf1tRn4SWiw3te5djgdYZ6k/oI2peVKVuRF4fn9tBb6dNqcmzU5L/qw
IFAGbHrQgLKm+a/sRxmPUDgH3KKHOVj4utWp+UhnMJbulHheb4mjUcAwhmahRWa6
VOujw5H5SNz/0egwLX0tdHA114gk957EWW67c4cX8jJGKLhD+rcdqsq08p8kDi1L
93FcXmn/6pUCyziKrlA4b9v7LWIbxcceVOF34GfID5yHI9Y/QCB/IIDEgEw+OyQm
jgSubJrIqg0CAwEAAaNCMEAwDwYDVR0TAQH/BAUwAwEB/zAOBgNVHQ8BAf8EBAMC
AYYwHQYDVR0OBBYEFIQYzIU07LwMlJQuCFmcx7IQTgoIMA0GCSqGSIb3DQEBCwUA
A4IBAQCY8jdaQZChGsV2USggNiMOruYou6r4lK5IpDB/G/wkjUu0yKGX9rbxenDI
U5PMCCjjmCXPI6T53iHTfIUJrU6adTrCC2qJeHZERxhlbI1Bjjt/msv0tadQ1wUs
N+gDS63pYaACbvXy8MWy7Vu33PqUXHeeE6V/Uq2V8viTO96LXFvKWlJbYK8U90vv
o/ufQJVtMVT8QtPHRh8jrdkPSHCa2XV4cdFyQzR1bldZwgJcJmApzyMZFo6IQ6XU
5MsI+yMRQ+hDKXJioaldXgjUkK642M4UwtBV8ob2xJNDd2ZhwLnoQdeXeGADbkpy
rqXRfboQnoZsG4q5WTP468SQvvG5
-----END CERTIFICATE-----)EOF";

static bool writeFile(const char *path, const char *data)
{
  size_t dataLen = strlen(data);
  Serial.printf("  write %s (data len=%u)\n", path, dataLen);
  if (dataLen == 0)
  {
    Serial.println("ERROR: data is empty");
    return false;
  }
  File f = SPIFFS.open(path, "w");
  if (!f)
  {
    Serial.printf("ERROR: open failed: %s\n", path);
    return false;
  }
  size_t written = f.write((const uint8_t *)data, dataLen);
  f.close();
  Serial.printf("  %s: written=%u / %u\n", path, written, dataLen);
  return written == dataLen;
}

void setup()
{
  Serial.begin(115200);
  delay(1000);
  Serial.println("=== PROVISIONING START ===");
  Serial.println("=== PROVISIONING START ===");
  Serial.println("=== PROVISIONING START ===");
  Serial.println("=== PROVISIONING START ===");

  // SPIFFS マウント（未フォーマットなら自動フォーマット）
  if (!SPIFFS.begin(true))
  {
    Serial.println("ERROR: SPIFFS mount failed");
    return;
  }

  Serial.println("Writing certs to SPIFFS...");
  if (!writeFile("/certs/ca.crt", AMAZON_ROOT_CA) ||
      !writeFile("/certs/device.crt", PROV_DEVICE_CERT) ||
      !writeFile("/certs/device.key", PROV_DEVICE_KEY))
  {
    Serial.println("ERROR: cert write failed");
    return;
  }

  Serial.println("Writing mqtt_host to NVS...");
  Preferences prefs;
  prefs.begin("device", false);
  prefs.putString("mqtt_host", PROV_MQTT_HOST);
  prefs.end();
  Serial.printf("  mqtt_host = %s\n", PROV_MQTT_HOST);

  Serial.println("=== PROVISIONING COMPLETE ===");
}

void loop() {}
