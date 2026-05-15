#include "command.h"
#include "jobs.h"
#include "logger.h"
#include "../device/ina228.h"
#include "../config.h"
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

  if (strcmp(job.operation, "charge_start") == 0)
  {
    setCharging(true);
    logger.println("[CMD] charge_start: charging enabled");
    jobsReport(job.id, "SUCCEEDED");
    return true;
  }

  if (strcmp(job.operation, "charge_stop") == 0)
  {
    setCharging(false);
    logger.println("[CMD] charge_stop: charging disabled");
    jobsReport(job.id, "SUCCEEDED");
    return true;
  }

  logger.printf("[CMD] unknown operation: %s\n", job.operation);
  jobsReport(job.id, "FAILED", "unknown operation");
  return false;
}
