#include "button.h"

Button button;

void Button::begin(uint8_t pin0, uint8_t pin1) {
  _pin0 = pin0;
  _pin1 = pin1;
  pinMode(_pin0, INPUT_PULLUP);
  pinMode(_pin1, INPUT_PULLUP);
}

ButtonEvent Button::read() {
  bool btn0 = digitalRead(_pin0) == LOW;
  bool btn1 = digitalRead(_pin1) == LOW;
  unsigned long now = millis();
  ButtonEvent ev = ButtonEvent::NONE;

  // BTN0: リリース時に SHORT を発火
  if (!btn0 && _btn0Prev) {
    ev = ButtonEvent::BTN0_SHORT;
  }
  _btn0Prev = btn0;

  // BTN1: 保持中に LONG_PRESS_MS 経過で LONG、リリース時に SHORT
  if (btn1 && !_btn1Prev) {
    _btn1PressTime = now;
    _btn1LongFired = false;
  }
  if (btn1 && !_btn1LongFired && (now - _btn1PressTime) >= LONG_PRESS_MS) {
    _btn1LongFired = true;
    if (ev == ButtonEvent::NONE) ev = ButtonEvent::BTN1_LONG;
  }
  if (!btn1 && _btn1Prev && !_btn1LongFired) {
    if (ev == ButtonEvent::NONE) ev = ButtonEvent::BTN1_SHORT;
  }
  _btn1Prev = btn1;

  return ev;
}
