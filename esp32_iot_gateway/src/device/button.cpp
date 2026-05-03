#include "button.h"
#include "speaker.h"

Button button;

void Button::begin() {
  pinMode(BTN0_PIN, INPUT_PULLUP);
  pinMode(BTN1_PIN, INPUT_PULLUP);
}

bool Button::isDown(uint8_t btn) {
  return digitalRead(btn == 0 ? BTN0_PIN : BTN1_PIN) == LOW;
}

ButtonEvent Button::read() {
  bool btn0 = digitalRead(BTN0_PIN) == LOW;
  bool btn1 = digitalRead(BTN1_PIN) == LOW;
  unsigned long now = millis();
  ButtonEvent ev = ButtonEvent::NONE;

  // BTN0: 押し込み瞬間にクリック音、リリース時に SHORT を発火
  if (btn0 && !_btn0Prev) {
    playTone(1200, 15);
  }
  if (!btn0 && _btn0Prev) {
    ev = ButtonEvent::BTN0_SHORT;
  }
  _btn0Prev = btn0;

  // BTN1: 押し込み瞬間にクリック音、LONG_PRESS_MS 到達で LONG 音＋イベント、リリース時に SHORT
  if (btn1 && !_btn1Prev) {
    playTone(1200, 15);
    _btn1PressTime = now;
    _btn1LongFired = false;
  }
  if (btn1 && !_btn1LongFired && (now - _btn1PressTime) >= LONG_PRESS_MS) {
    _btn1LongFired = true;
    playTone(600, 40);
    if (ev == ButtonEvent::NONE) ev = ButtonEvent::BTN1_LONG;
  }
  if (!btn1 && _btn1Prev && !_btn1LongFired) {
    if (ev == ButtonEvent::NONE) ev = ButtonEvent::BTN1_SHORT;
  }
  _btn1Prev = btn1;

  return ev;
}
