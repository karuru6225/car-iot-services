#include "button.h"
#include "speaker.h"
#include "../board_pins.h"

Button button;

void Button::begin() {
  _btn0Pin = boardPins().btn0Pin;
  _btn1Pin = boardPins().btn1Pin;
  pinMode(_btn0Pin, INPUT_PULLUP);
  pinMode(_btn1Pin, INPUT_PULLUP);
}

bool Button::isDown(uint8_t btn) {
  return digitalRead(btn == 0 ? _btn0Pin : _btn1Pin) == LOW;
}

ButtonEvent Button::read() {
  bool btn0 = digitalRead(_btn0Pin) == LOW;
  bool btn1 = digitalRead(_btn1Pin) == LOW;
  unsigned long now = millis();
  ButtonEvent ev = ButtonEvent::NONE;

  // BTN0: 押し込み瞬間にクリック音、LONG_PRESS_MS 到達で LONG 音＋イベント、リリース時に SHORT
  if (btn0 && !_btn0Prev) {
    playTone(1200, 15);
    _btn0PressTime = now;
    _btn0LongFired = false;
  }
  if (btn0 && !_btn0LongFired && (now - _btn0PressTime) >= LONG_PRESS_MS) {
    _btn0LongFired = true;
    playTone(600, 40);
    if (ev == ButtonEvent::NONE) ev = ButtonEvent::BTN0_LONG;
  }
  if (!btn0 && _btn0Prev && !_btn0LongFired) {
    if (ev == ButtonEvent::NONE) ev = ButtonEvent::BTN0_SHORT;
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
