#include "command.h"
#include "jobs.h"
#include "logger.h"
#include "../device/ina228.h"
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

  logger.printf("[CMD] 未知の operation: %s\n", job.operation);
  jobsReport(job.id, "FAILED", "unknown operation");
  return false;
}
