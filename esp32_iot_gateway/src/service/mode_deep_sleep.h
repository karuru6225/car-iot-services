#pragma once
#include "operation_mode.h"

// DEEP_SLEEP モードのハンドラ。
// beforeRun(): BLE接続によるCONTINUOUSへの自動昇格判定
// run(): shadow同期 → LTE切断 → DeepSleep突入（戻らない）
class DeepSleepModeHandler : public IOperationModeHandler
{
public:
  explicit DeepSleepModeHandler(OperationModeContext &ctx) : _ctx(ctx) {}
  void beforeRun() override;
  void run() override;

private:
  OperationModeContext &_ctx;
};

extern DeepSleepModeHandler deepSleepMode;
