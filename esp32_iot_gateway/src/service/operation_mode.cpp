#include "operation_mode.h"
#include "../logger.h"

OperationModeManager modeManager(modeCtx);

void OperationModeManager::registerMode(OperationMode mode, IOperationModeHandler *handler)
{
  _handlers[static_cast<int>(mode)] = handler;
}

void OperationModeManager::runCurrent()
{
  IOperationModeHandler *handler = _handlers[static_cast<int>(_ctx.mode())];
  if (!handler)
  {
    logger.printf("[MODE] 未登録の動作モードです: %d\n", (int)_ctx.mode());
    return;
  }

  handler->beforeRun();

  // beforeRun() 内でモードが変わっていたら、新しいモードのハンドラに追従する
  handler = _handlers[static_cast<int>(_ctx.mode())];
  if (!handler)
  {
    logger.printf("[MODE] 未登録の動作モードです: %d\n", (int)_ctx.mode());
    return;
  }

  handler->run();
}
