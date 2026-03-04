#pragma once
// AWS IoT Core 接続用証明書
// infra/gen_certs.ps1 で再生成すること（terraform apply 後）

#define MQTT_HOST "xxxxxxxxxxxxx-ats.iot.ap-northeast-1.amazonaws.com"
#define MQTT_PORT 8883
#define MQTT_CLIENT_ID "client-id-001"
#define MQTT_TOPIC_DATA "sensors/client-id-001/data"

static const char AWS_ROOT_CA[] PROGMEM = R"EOF(-----BEGIN CERTIFICATE-----
MIIDQTCCAimgAwIBAgITBmyfz5m/jAo54vB4ikPmljZbyjANBgkqhkiG9w0BAQsF
...
MVLe6A==
-----END CERTIFICATE-----)EOF";

static const char DEVICE_CERT[] PROGMEM = R"EOF(-----BEGIN CERTIFICATE-----
MIIDWTCCAkGgAwIBAgIUZODwx19tMn3iaAHJBABd6mfjnaYwDQYJKoZIhvcNAQEL
...
odAIW0CSOpA8y6XWZBXB4YN8y4CzBlf0MJqQaAbgcxYmfFvdg3A/RDbagJ1W
-----END CERTIFICATE-----
)EOF";

static const char DEVICE_KEY[] PROGMEM = R"EOF(-----BEGIN RSA PRIVATE KEY-----
MIIEpAIBAAKCAQEA6cVeOBMy2FWYZLSuLR2B97ZR4IxIc3gl07lgs55BqAJpf3gB
...
R+cwjgQwTSqQKeukT+t3nRQQsqPH0Sh4u7lmlarwvi5ITsNGstTJJA==
-----END RSA PRIVATE KEY-----
)EOF";