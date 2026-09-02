#pragma once
#include "mode_context.h"

// 動作モードハンドラの基底インターフェース。
// beforeRun() は「現在のモードから別モードへ切り替えるべきか」の判定に使う
// （例: DeepSleepModeHandler での BLE 接続による CONTINUOUS 自動昇格判定）。
// 判定不要なモードは override 不要（デフォルトで何もしない）。
// モードを変更したい場合は各ハンドラが自身のコンストラクタで受け取った
// OperationModeContext の setMode() を呼ぶ（本クラス・OperationModeManagerには依存しない）
class IOperationModeHandler
{
public:
  virtual ~IOperationModeHandler() = default;

  virtual void beforeRun() {}
  virtual void run() = 0;
};

// 動作モードと、そのモードを実行するハンドラインスタンスを対応付けるレジストリ。
// モード状態自体は持たず、コンストラクタで受け取った OperationModeContext を参照するのみ。
// main.cpp の setup() でモードごとにハンドラを登録し、loop() では runCurrent() を呼ぶだけにする
class OperationModeManager
{
public:
  explicit OperationModeManager(OperationModeContext &ctx) : _ctx(ctx) {}

  // mode に対応するハンドラを登録する（setup() で1回だけ呼ぶ想定。所有権は呼び出し側=グローバルインスタンスが持つ）
  void registerMode(OperationMode mode, IOperationModeHandler *handler);

  // 現在のモード（_ctx.mode()）のハンドラに対し
  // beforeRun() → (モード変更があれば追従) → run() の順に呼び出す
  void runCurrent();

private:
  OperationModeContext &_ctx;
  static const int kModeCount = 4; // OperationMode の要素数（config.h 参照）
  IOperationModeHandler *_handlers[kModeCount] = {};
};

extern OperationModeManager modeManager;
