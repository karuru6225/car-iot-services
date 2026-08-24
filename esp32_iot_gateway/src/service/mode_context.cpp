#include "mode_context.h"
#include "../logger.h"

OperationModeContext modeCtx;

const char *operationModeName(OperationMode m)
{
  switch (m)
  {
  case OperationMode::DEEP_SLEEP:
    return "DEEP_SLEEP";
  case OperationMode::CONTINUOUS:
    return "CONTINUOUS";
  case OperationMode::TIMED_CONTINUOUS:
    return "TIMED_CONTINUOUS";
  case OperationMode::LIGHT_SLEEP:
    return "LIGHT_SLEEP";
  }
  return "?";
}

void OperationModeContext::setMode(OperationMode m)
{
  if (m == _mode)
    return;
  logger.printf("[MODE_CTX] mode: %s -> %s\n", operationModeName(_mode), operationModeName(m));
  _mode = m;
}

void OperationModeContext::setBleUpgradedToContinuous(bool v)
{
  if (v == _bleUpgradedToContinuous)
    return;
  logger.printf("[MODE_CTX] bleUpgradedToContinuous: %d -> %d\n", _bleUpgradedToContinuous, v);
  _bleUpgradedToContinuous = v;
}

void OperationModeContext::setCanUpgradedToContinuous(bool v)
{
  if (v == _canUpgradedToContinuous)
    return;
  logger.printf("[MODE_CTX] canUpgradedToContinuous: %d -> %d\n", _canUpgradedToContinuous, v);
  _canUpgradedToContinuous = v;
}

void OperationModeContext::setPromotedFromMode(OperationMode m)
{
  if (m == _promotedFromMode)
    return;
  logger.printf("[MODE_CTX] promotedFromMode: %s -> %s\n", operationModeName(_promotedFromMode), operationModeName(m));
  _promotedFromMode = m;
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
