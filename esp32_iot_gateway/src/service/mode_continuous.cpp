#include "mode_continuous.h"
#include "mode_context.h"
#include "mode_common.h"
#include "../config.h"

ContinuousModeHandler continuousMode(modeCtx);

void ContinuousModeHandler::run()
{
  continuousLoopCore();
#ifndef DEBUG_SKIP_NETWORK
  checkAndHandleJob(); // 5分サイクルごとにOTA/コマンドJobsを確認する
#endif
}
