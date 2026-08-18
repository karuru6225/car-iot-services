#pragma once
#include "mode_continuous_base.h"

// TIMED_CONTINUOUS モードのハンドラ。continuousUntilEpoch()の期限まで
// CONTINUOUSサイクルを繰り返し、期限到達で自動DEEP_SLEEPへ戻る
class TimedContinuousModeHandler : public ContinuousModeHandlerBase
{
public:
  explicit TimedContinuousModeHandler(OperationModeContext &ctx) : ContinuousModeHandlerBase(ctx) {}
  void run() override;

protected:
  OperationMode selfMode() const override { return OperationMode::TIMED_CONTINUOUS; }
};

extern TimedContinuousModeHandler timedContinuousMode;
