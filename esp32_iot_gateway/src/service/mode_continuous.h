#pragma once
#include "mode_continuous_base.h"

// CONTINUOUS モードのハンドラ。BTN1長押しやBLE接続で無期限にCONTINUOUSへ留まる
class ContinuousModeHandler : public ContinuousModeHandlerBase
{
public:
  explicit ContinuousModeHandler(OperationModeContext &ctx) : ContinuousModeHandlerBase(ctx) {}
  void run() override;

protected:
  OperationMode selfMode() const override { return OperationMode::CONTINUOUS; }
};

extern ContinuousModeHandler continuousMode;
