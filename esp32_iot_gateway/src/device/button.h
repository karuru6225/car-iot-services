#pragma once
#include <Arduino.h>

enum class ButtonEvent { NONE, BTN0_SHORT, BTN1_SHORT, BTN1_LONG };

class Button {
public:
  void begin();
  ButtonEvent read();
  bool isDown(uint8_t btn); // 0=BTN0, 1=BTN1

private:
  static const uint8_t BTN0_PIN = 26;
  static const uint8_t BTN1_PIN = 33;
  static const uint32_t LONG_PRESS_MS = 1000;

  bool _btn0Prev = false;
  bool _btn1Prev = false;
  unsigned long _btn1PressTime = 0;
  bool _btn1LongFired = false;
};

extern Button button;
