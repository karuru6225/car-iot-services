#include "mode_common.h"
#include "mode_context.h"
#include "monitor.h"
#include "jobs.h"
#include "command.h"
#include "ota.h"
#include "../device/ble_scan.h"
#include "../domain/charging.h"
#include "../board_pins.h"
#include "../config.h"
#include "../logger.h"
#include <Arduino.h>
#include <string.h>
#include <time.h>

void updateChargingState()
{
  float vMain = modeCtx.lastResult().reading.main.voltage;
  float vSub = modeCtx.lastResult().reading.sub.voltage;
  ChargingThresholds th{getChgStartV(), getChgStopV(), getChgMinDiffV()};

  bool wasCharging = isCharging();
  bool shouldCharge = decideCharging(vMain, vSub, wasCharging, th);
  if (shouldCharge == wasCharging)
    return;

  setCharging(shouldCharge);
  digitalWrite(boardPins().chgOnPin, shouldCharge ? HIGH : LOW);
  float diff = vSub - vMain;
  if (shouldCharge)
    logger.printf("[MAIN] auto charge ON  vMain=%.2fV < startV=%.2fV diff=%.2fV\n", vMain, th.startV, diff);
  else
    logger.printf("[MAIN] auto charge OFF vMain=%.2fV stopV=%.2fV diff=%.2fV minDiff=%.2fV\n",
                  vMain, th.stopV, diff, th.minDiff);
}

uint32_t secsToNextBoundary()
{
  time_t now = time(nullptr);
  if (now <= 1577836800L) // 2020-01-01以前なら時刻未同期
    return SLEEP_INTERVAL_SEC;
  time_t next = ((now / (time_t)SLEEP_INTERVAL_SEC) + 1) * (time_t)SLEEP_INTERVAL_SEC;
  return (uint32_t)(next - now);
}

void pollBleCollect()
{
  if (!modeCtx.blePending())
    return;
  MeasureResult r = modeCtx.lastResult();
  if (!collectBle(r))
    return;
  modeCtx.setLastResult(r);
  publishBle(r);
  modeCtx.setBlePending(false);
}

void checkAndHandleJob()
{
  JobInfo job;
  if (jobsGetNext(job))
  {
    if (strcmp(job.operation, "ota") == 0)
      ota.handleJob(job); // 成功時は esp_restart() するため戻らない
    else
      commandHandleJob(job);
  }
}
