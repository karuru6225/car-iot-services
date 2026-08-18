#pragma once
#include "../config.h"

// 動作モードと、そのモードで実行される関数を対応付けるレジストリ。
// main.cpp の setup() でモードごとに関数を登録し、loop() では登録された関数を呼ぶだけにする。
class OperationModeManager
{
public:
  using ModeFunc = void (*)();

  // mode に対応する関数を登録する（setup() で1回だけ呼ぶ想定）
  void registerMode(OperationMode mode, ModeFunc func);

  // mode に登録された関数を呼び出す。未登録なら警告ログを出して何もしない
  void run(OperationMode mode);

private:
  static const int kModeCount = 4; // OperationMode の要素数（config.h 参照）
  ModeFunc _handlers[kModeCount] = {};
};

extern OperationModeManager modeManager;
