#pragma once
#include <Arduino.h>

enum class ButtonEvent { NONE, BTN0_SHORT, BTN1_SHORT, BTN1_LONG };

class Button {
public:
  void begin(uint8_t pin0, uint8_t pin1);
  ButtonEvent read();

private:
  uint8_t _pin0 = 0;
  uint8_t _pin1 = 0;
  bool _btn0Prev = false;
  bool _btn1Prev = false;
  unsigned long _btn1PressTime = 0;
  bool _btn1LongFired = false;

  static const uint32_t LONG_PRESS_MS = 1000;
};

extern Button button;
