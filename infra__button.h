#pragma once
#include <Arduino.h>

enum BtnEvent
{
  BTN_NONE,
  BTN_SHORT,
  BTN_LONG
};

class Button
{
public:
  void     setup();
  BtnEvent check();
  BtnEvent waitFor(uint32_t ms);
};

extern Button button;
