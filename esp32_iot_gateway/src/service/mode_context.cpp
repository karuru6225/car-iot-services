#include "mode_context.h"
#include "../logger.h"

OperationModeContext modeCtx;

namespace
{
const char *modeName(OperationMode m)
{
  switch (m)
  {
  case OperationMode::DEEP_SLEEP:
    return "DEEP_SLEEP";
  case OperationMode::CONTINUOUS:
    return "CONTINUOUS";
  case OperationMode::TIMED_CONTINUOUS:
    return "TIMED_CONTINUOUS";
  }
  return "?";
}
} // namespace

void OperationModeContext::setMode(OperationMode m)
{
  if (m == _mode)
    return;
  logger.printf("[MODE_CTX] mode: %s -> %s\n", modeName(_mode), modeName(m));
  _mode = m;
}

void OperationModeContext::setBleUpgradedToContinuous(bool v)
{
  if (v == _bleUpgradedToContinuous)
    return;
  logger.printf("[MODE_CTX] bleUpgradedToContinuous: %d -> %d\n", _bleUpgradedToContinuous, v);
  _bleUpgradedToContinuous = v;
}

void OperationModeContext::setUserForcedSleep(bool v)
{
  if (v == _userForcedSleep)
    return;
  logger.printf("[MODE_CTX] userForcedSleep: %d -> %d\n", _userForcedSleep, v);
  _userForcedSleep = v;
}

void OperationModeContext::setContinuousUntilEpoch(time_t v)
{
  if (v == _continuousUntilEpoch)
    return;
  logger.printf("[MODE_CTX] continuousUntilEpoch: %ld -> %ld\n", (long)_continuousUntilEpoch, (long)v);
  _continuousUntilEpoch = v;
}
