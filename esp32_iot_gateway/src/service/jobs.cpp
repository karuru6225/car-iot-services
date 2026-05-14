#include "jobs.h"
#include "mqtt.h"
#include "logger.h"
#include "../config.h"
#include <ArduinoJson.h>
#include <Arduino.h>
#include <cstdio>

static void topicAccepted(char *buf, size_t len)
{
  snprintf(buf, len, "$aws/things/%s/jobs/$next/get/accepted", getDeviceId());
}

static void topicRejected(char *buf, size_t len)
{
  snprintf(buf, len, "$aws/things/%s/jobs/$next/get/rejected", getDeviceId());
}

void jobsSetup()
{
  char topic[128];
  topicAccepted(topic, sizeof(topic));
  mqtt.subscribe(topic);
  topicRejected(topic, sizeof(topic));
  mqtt.subscribe(topic);
  delay(500); // SIM7080G のサブスクリプション確立を待つ
}

bool jobsGetNext(JobInfo &out)
{
  char getTopic[128];
  snprintf(getTopic, sizeof(getTopic), "$aws/things/%s/jobs/$next/get", getDeviceId());
  if (!mqtt.publish(getTopic, "{}"))
    return false;

  char recvTopic[128];
  static char payload[1024];
  if (!mqtt.pollMqtt(recvTopic, sizeof(recvTopic), payload, sizeof(payload), 5000))
  {
    logger.println("[JOBS] レスポンスなし");
    return false;
  }

  char rejected[128];
  topicRejected(rejected, sizeof(rejected));
  if (strstr(recvTopic, "rejected"))
  {
    logger.println("[JOBS] リクエスト拒否");
    return false;
  }

  JsonDocument doc;
  if (deserializeJson(doc, payload) != DeserializationError::Ok)
    return false;

  JsonObject exec = doc["execution"];
  if (exec.isNull())
  {
    logger.println("[JOBS] ジョブなし");
    return false;
  }

  const char *jobId    = exec["jobId"];
  const char *operation = exec["jobDocument"]["operation"];
  if (!jobId) return false;

  strncpy(out.id, jobId, sizeof(out.id) - 1);
  out.id[sizeof(out.id) - 1] = '\0';

  strncpy(out.operation, operation ? operation : "", sizeof(out.operation) - 1);
  out.operation[sizeof(out.operation) - 1] = '\0';

  serializeJson(exec["jobDocument"], out.document, sizeof(out.document));

  logger.printf("[JOBS] job=%s operation=%s\n", out.id, out.operation);
  return true;
}

bool jobsReport(const char *jobId, const char *status, const char *reason)
{
  char topic[160];
  snprintf(topic, sizeof(topic), "$aws/things/%s/jobs/%s/update", getDeviceId(), jobId);

  char payload[256];
  if (reason)
    snprintf(payload, sizeof(payload),
             "{\"status\":\"%s\",\"statusDetails\":{\"reason\":\"%s\"}}", status, reason);
  else
    snprintf(payload, sizeof(payload), "{\"status\":\"%s\"}", status);

  return mqtt.publish(topic, payload);
}
