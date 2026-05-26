#pragma once
#include <Arduino.h>
#include <stdint.h>

class Mqtt
{
public:
  bool publish(const char *topic, const uint8_t *data, size_t len);
  bool publish(const char *topic, const char *payload);
  bool subscribe(const char *topic);
  bool pollMqtt(char *outTopic, int topicSize,
                char *outPayload, int payloadSize,
                uint32_t timeoutMs = 3000);
  bool isConnected();

private:
  bool connect();
};

extern Mqtt mqtt;
