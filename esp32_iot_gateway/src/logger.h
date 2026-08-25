#pragma once
#include <Arduino.h>

class Logger
{
public:
  void init();
  void print(const char *msg);
  void println(const char *msg = "");
  void printf(const char *fmt, ...);

  // muted=trueの間はprint/println/printfを一切実行しない（Serial出力もSPIFFS保存も含めて
  // no-op）。CAN Proxyモード中はUSBシリアルをSLCANプロトコル専用にする必要があり、
  // BLEコールバック等どこから呼ばれるlogger出力も一括で抑止したいため、canInit()/canDeinit()
  // 個別のquiet引数ではなくLogger自体をミュートする設計にした（service/can_proxy.cpp参照）
  void setMuted(bool muted);

private:
  bool muted_ = false;
};

extern Logger logger;
