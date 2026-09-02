#include "mode_timed_continuous.h"
#include "mode_context.h"
#include "mode_common.h"
#include "../config.h"
#include "../logger.h"
#include <time.h>

TimedContinuousModeHandler timedContinuousMode(modeCtx);

// _ctx.continuousUntilEpoch()までCONTINUOUSサイクルを繰り返す。
// continuousLoopCore()はBTN1長押しでも即座にDEEP_SLEEPへ抜けられる
void TimedContinuousModeHandler::run()
{
  continuousLoopCore();
#ifndef DEBUG_SKIP_NETWORK
  checkAndHandleJob(); // 5分サイクルごとにOTA/コマンドJobsを確認する
#endif
  if (_ctx.mode() == OperationMode::TIMED_CONTINUOUS && time(nullptr) >= _ctx.continuousUntilEpoch())
  {
    logger.println("[MAIN] TIMED_CONTINUOUS 期限到達 → DEEP_SLEEP モードへ切り替え");
    _ctx.setMode(OperationMode::DEEP_SLEEP); // 次回 loop() で DEEP_SLEEP が実行される
  }
}
