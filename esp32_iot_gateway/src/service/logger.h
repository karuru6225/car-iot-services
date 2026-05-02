#pragma once
#include <Arduino.h>

class Logger
{
public:
  void init();
  void print(const char *msg);
  void println(const char *msg = "");
  void printf(const char *fmt, ...);
};

extern Logger logger;
