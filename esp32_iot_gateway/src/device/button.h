#pragma once
#include <Arduino.h>

enum class ButtonEvent { NONE, BTN0_SHORT, BTN0_LONG, BTN1_SHORT, BTN1_LONG };

class Button {
public:
  void begin();
  ButtonEvent read();
  bool isDown(uint8_t btn); // 0=BTN0, 1=BTN1

private:
  uint8_t _btn0Pin = 0;
  uint8_t _btn1Pin = 0;
  static const uint32_t LONG_PRESS_MS = 1000;

  bool _btn0Prev = false;
  unsigned long _btn0PressTime = 0;
  bool _btn0LongFired = false;
  bool _btn1Prev = false;
  unsigned long _btn1PressTime = 0;
  bool _btn1LongFired = false;
};

extern Button button;
