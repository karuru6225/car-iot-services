#include "command.h"
#include "jobs.h"
#include "logger.h"
#include "../device/ina228.h"
#include "../device/ads.h"
#include "../device/oled.h"
#include "../config.h"
#include <ArduinoJson.h>
#include <Arduino.h>
#include <cstring>

bool commandHandleJob(const JobInfo &job)
{
  logger.printf("[CMD] operation: %s\n", job.operation);

  if (strcmp(job.operation, "ah_reset") == 0)
  {
    ina228.resetCharge();
    logger.println("[CMD] ah_reset: 完了");
    jobsReport(job.id, "SUCCEEDED");
    return true;
  }

  if (strcmp(job.operation, "charge_main_batt") == 0)
  {
    JsonDocument doc;
    deserializeJson(doc, job.document);
    uint32_t timeoutSec = doc["timeout_sec"] | 1200u; // デフォルト 20 分

    float vMain = adsReadDiffMain();
    float vSub  = adsReadDiffSub();

    if (vSub <= vMain)
    {
      char reason[32];
      snprintf(reason, sizeof(reason), "sub(%.2f)<=main(%.2f)", vSub, vMain);
      logger.printf("[CMD] charge_main_batt 不可: %s\n", reason);
      jobsReport(job.id, "FAILED", reason);
      return false;
    }

    logger.printf("[CMD] charge_main_batt 開始: %u sec\n", timeoutSec);
    digitalWrite(CHG_ON_PIN, HIGH);

    unsigned long startMs   = millis();
    unsigned long timeoutMs = timeoutSec * 1000UL;
    unsigned long lastReadMs = 0;

    while (millis() - startMs < timeoutMs)
    {
      if (millis() - lastReadMs >= 5000)
      {
        vMain = adsReadDiffMain();
        vSub  = adsReadDiffSub();
        lastReadMs = millis();
        int remainSec = (int)((timeoutMs - (millis() - startMs)) / 1000);
        oledShowCharging(vMain, vSub, remainSec);
        logger.printf("[CMD] M:%.2fV S:%.2fV remain:%ds\n", vMain, vSub, remainSec);
      }
      delay(100);
    }

    digitalWrite(CHG_ON_PIN, LOW);
    logger.println("[CMD] charge_main_batt 完了");
    jobsReport(job.id, "SUCCEEDED");
    return true;
  }

  logger.printf("[CMD] 未知の operation: %s\n", job.operation);
  jobsReport(job.id, "FAILED", "unknown operation");
  return false;
}
