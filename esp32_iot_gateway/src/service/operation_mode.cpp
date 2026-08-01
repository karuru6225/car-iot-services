#include "operation_mode.h"
#include "logger.h"

OperationModeManager modeManager;

void OperationModeManager::registerMode(OperationMode mode, ModeFunc func)
{
  _handlers[static_cast<int>(mode)] = func;
}

void OperationModeManager::run(OperationMode mode)
{
  ModeFunc f = _handlers[static_cast<int>(mode)];
  if (f)
    f();
  else
    logger.printf("[MODE] 未登録の動作モードです: %d\n", (int)mode);
}
