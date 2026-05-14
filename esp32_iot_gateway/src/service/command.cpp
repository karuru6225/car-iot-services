#include "command.h"
#include "jobs.h"
#include "logger.h"
#include "../device/ina228.h"
#include "../device/ads.h"
#include "../config.h"
#include <ArduinoJson.h>
#include <cstring>

bool commandHandleJob(const JobInfo &job)
{
  logger.printf("[CMD] operation: %s\n", job.operation);

  if (strcmp(job.operation, "ah_reset") == 0)
  {
    ina228.resetCharge();
    logger.println("[CMD] ah_reset: done");
    jobsReport(job.id, "SUCCEEDED");
    return true;
  }

  if (strcmp(job.operation, "charge_main_batt") == 0)
  {
    JsonDocument doc;
    deserializeJson(doc, job.document);
    uint32_t timeoutSec = doc["timeout_sec"] | 1200u;

    float vMain = adsReadDiffMain();
    float vSub  = adsReadDiffSub();

    if (vSub <= vMain)
    {
      char reason[40];
      snprintf(reason, sizeof(reason), "sub(%.2f)<=main(%.2f)", vSub, vMain);
      logger.printf("[CMD] charge_main_batt skipped: %s\n", reason);
      jobsReport(job.id, "FAILED", reason);
      return false;
    }

    // 残り時間を RTC メモリに記録して return（5分ごとに充電 sleep を繰り返す）
    jobsReport(job.id, "IN_PROGRESS");
    initCharge(timeoutSec, job.id);
    logger.printf("[CMD] charge_main_batt: %u sec remaining\n", timeoutSec);
    return true;
  }

  logger.printf("[CMD] unknown operation: %s\n", job.operation);
  jobsReport(job.id, "FAILED", "unknown operation");
  return false;
}
