#include "charging.h"

bool decideCharging(float vMain, float vSub, bool currentlyCharging, const ChargingThresholds &th)
{
  float diff = vSub - vMain;

  if (vMain >= 10.0f && !currentlyCharging && vMain < th.startV && diff >= th.minDiff)
    return true;

  if (vMain >= 10.0f && currentlyCharging && (vMain >= th.stopV || diff < th.minDiff))
    return false;

  return currentlyCharging;
}
