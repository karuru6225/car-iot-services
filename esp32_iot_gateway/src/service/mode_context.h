#pragma once
#include <time.h>
#include "monitor.h" // MeasureResult
#include "../config.h" // OperationMode

// モードハンドラ間で共有する実行時状態。アプリ全体で1インスタンスのみ生成し、
// OperationModeManagerと各モードハンドラのコンストラクタに参照として渡す。
// 状態遷移系プロパティ(mode等)はsetter経由でのみ変更させ、変更時にログを出す。
class OperationModeContext
{
public:
  OperationMode mode() const { return _mode; }
  void setMode(OperationMode m);

  bool bleUpgradedToContinuous() const { return _bleUpgradedToContinuous; }
  void setBleUpgradedToContinuous(bool v);

  bool canUpgradedToContinuous() const { return _canUpgradedToContinuous; }
  void setCanUpgradedToContinuous(bool v);

  bool userForcedSleep() const { return _userForcedSleep; }
  void setUserForcedSleep(bool v);

  time_t continuousUntilEpoch() const { return _continuousUntilEpoch; }
  void setContinuousUntilEpoch(time_t v);

  // 高頻度更新値（毎ループ/毎秒更新）。ログは出さない単純アクセサ
  const MeasureResult &lastResult() const { return _lastResult; }
  void setLastResult(const MeasureResult &r) { _lastResult = r; }

  bool blePending() const { return _blePending; }
  void setBlePending(bool v) { _blePending = v; }

private:
  OperationMode _mode = OperationMode::DEEP_SLEEP;
  MeasureResult _lastResult = {};
  bool _blePending = false;
  bool _bleUpgradedToContinuous = false;
  bool _canUpgradedToContinuous = false;
  bool _userForcedSleep = false;
  time_t _continuousUntilEpoch = 0;
};

// アプリケーション全体で共有する唯一のインスタンス
extern OperationModeContext modeCtx;
