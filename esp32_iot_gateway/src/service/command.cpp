#include "command.h"
#include "jobs.h"
#include "logger.h"
#include "../device/ina228.h"
#include "../device/ads.h"
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
    float vMain = adsReadDiffMain();
    float vSub = adsReadDiffSub();

    if (vSub <= vMain)
    {
      char reason[40];
      snprintf(reason, sizeof(reason), "sub(%.2f)<=main(%.2f)", vSub, vMain);
      logger.printf("[CMD] charge_main_batt skipped: %s\n", reason);
      jobsReport(job.id, "FAILED", reason);
      return false;
    }

    jobsReport(job.id, "SUCCEEDED");
    logger.printf("[CMD] charge_main_batt: done\n");
    return true;
  }

  logger.printf("[CMD] unknown operation: %s\n", job.operation);
  jobsReport(job.id, "FAILED", "unknown operation");
  return false;
}
