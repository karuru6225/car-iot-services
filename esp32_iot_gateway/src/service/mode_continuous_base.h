#pragma once
#include "operation_mode.h"

// CONTINUOUS / TIMED_CONTINUOUS 系モード共通の5分待機ループを持つ基底クラス。
// beforeRun(): BLE切断 → DEEP_SLEEP降格判定（BLE接続で昇格した場合のみ）
// selfMode()の差分のみサブクラスが実装する
class ContinuousModeHandlerBase : public IOperationModeHandler
{
public:
  explicit ContinuousModeHandlerBase(OperationModeContext &ctx) : _ctx(ctx) {}
  void beforeRun() override;

protected:
  OperationModeContext &_ctx;

  // このハンドラが担当する動作モード（BTN1長押しでの離脱判定・待機ループの継続条件に使う）
  virtual OperationMode selfMode() const = 0;

  // 次の5分境界（UTC）まで待機しながらボタン監視・カウントダウン表示・BLE Notifyを行う共通処理。
  // サブクラスの run() から呼ぶ
  void continuousLoopCore();

private:
  // メニューへの遷移（BTN0 短押し）。CONTINUOUS 系モード共通の処理
  void handleMenuButton();

  // このサイクル終了後に遷移する動作モードを予測する（OLED表示用）。
  // 実際の遷移判定（beforeRun/各サブクラスのrun()末尾）と条件を揃えてある
  const char *nextModeLabel() const;

  // 1秒ティックの追加処理。OLED は電圧/電流画面を優先するため OBD 結果は出さず、
  // ログ出力と BLE Notify にのみ使う。AWSへの送信は未実装。両サブクラス共通のため非virtual
  void onTick();
};
