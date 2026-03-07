#pragma once
#include <Arduino.h>

// SerialAT (SIM7080G) との双方向転送モード。
// シリアルから "exit" + Enter で抜ける。
// .ino の DEBUG_MODE セクションから呼び出す。
// 切り離す場合: このファイルと bypass_mode.cpp を削除し、
//   .ino の bypassMode.run() 呼び出しと #include を削除する。

class BypassMode
{
public:
  // blocks until "exit" is entered
  void run();
};

extern BypassMode bypassMode;
