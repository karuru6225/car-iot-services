#include "shadow.h"
#include "mqtt.h"
#include "logger.h"
#include "../config.h"
#include "../domain/telemetry.h"
#include <ArduinoJson.h>
#include <cstdio>

static void deltaTopic(char *buf, size_t len)
{
  snprintf(buf, len, "$aws/things/%s/shadow/update/delta", getDeviceId());
}

void shadowPublishConfig(bool clearDesired)
{
  char topic[128];
  snprintf(topic, sizeof(topic), "$aws/things/%s/shadow/update", getDeviceId());

  char payload[256];
  buildConfigPayload(payload, sizeof(payload), clearDesired);

  if (mqtt.publish(topic, payload))
    logger.println("[SHADOW] config published");
  else
    logger.println("[SHADOW] config publish failed");
}

void shadowSetup()
{
  char topic[128];
  deltaTopic(topic, sizeof(topic));

  if (mqtt.subscribe(topic))
    logger.println("[SHADOW] delta subscribed");
  else
    logger.println("[SHADOW] delta subscribe failed");
}

bool shadowPollDelta(uint32_t timeoutMs)
{
  char recvTopic[128];
  static char payload[512];

  if (!mqtt.pollMqtt(recvTopic, sizeof(recvTopic), payload, sizeof(payload), timeoutMs))
    return false;

  char expected[128];
  deltaTopic(expected, sizeof(expected));
  if (strcmp(recvTopic, expected) != 0)
    return false;

  JsonDocument doc;
  if (deserializeJson(doc, payload) != DeserializationError::Ok)
    return false;

  JsonObject state = doc["state"];
  if (state.isNull())
    return false;

  bool changed = false;

  if (state["ah_offset"].is<int32_t>())
  {
    setAhOffset(state["ah_offset"].as<int32_t>());
    logger.printf("[SHADOW] ah_offset → %d\n", getAhOffset());
    changed = true;
  }

  if (state["relay_mode"].is<const char *>())
  {
    const char *mode = state["relay_mode"];
    setRelayMode(strcmp(mode, "off") == 0
                     ? RelayMode::RELAY_OFF
                     : RelayMode::SLEEP_INDICATOR);
    logger.printf("[SHADOW] relay_mode → %s\n", mode);
    changed = true;
  }

  if (state["debug_log"].is<bool>())
  {
    bool en = state["debug_log"].as<bool>();
    setDebugLogEnabled(en);
    logger.printf("[SHADOW] debug_log → %s\n", en ? "on" : "off");
    changed = true;
  }

  if (state["chg_start_v"].is<float>())
  {
    setChgStartV(state["chg_start_v"].as<float>());
    logger.printf("[SHADOW] chg_start_v → %.2f\n", getChgStartV());
    changed = true;
  }

  if (state["chg_stop_v"].is<float>())
  {
    setChgStopV(state["chg_stop_v"].as<float>());
    logger.printf("[SHADOW] chg_stop_v → %.2f\n", getChgStopV());
    changed = true;
  }

  if (state["charging"].is<bool>())
  {
    setCharging(state["charging"].as<bool>());
    logger.printf("[SHADOW] charging → %s\n", isCharging() ? "on" : "off");
    changed = true;
  }

  if (changed)
    shadowPublishConfig(true); // reported を更新して desired をクリア

  return changed;
}
