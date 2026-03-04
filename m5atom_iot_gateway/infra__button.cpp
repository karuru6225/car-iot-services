#include "infra__button.h"
#include "config.h"

static volatile uint32_t sPressStart = 0;
static volatile bool     sShortFired = false;
static volatile bool     sLongFired  = false;

static void IRAM_ATTR btnISR()
{
  static uint32_t lastEdge = 0;
  uint32_t now = millis();

  if (now - lastEdge < DEBOUNCE_MS) return;
  lastEdge = now;

  if (digitalRead(BTN_PIN) == LOW)
  {
    sPressStart = now;
  }
  else
  {
    uint32_t dur = now - sPressStart;
    if (dur >= LONG_PRESS_MS)
      sLongFired = true;
    else if (dur >= DEBOUNCE_MS)
      sShortFired = true;
  }
}

Button button;

void Button::setup()
{
  attachInterrupt(digitalPinToInterrupt(BTN_PIN), btnISR, CHANGE);
}

BtnEvent Button::check()
{
  if (sLongFired)  { sLongFired  = false; return BTN_LONG;  }
  if (sShortFired) { sShortFired = false; return BTN_SHORT; }
  return BTN_NONE;
}

BtnEvent Button::waitFor(uint32_t ms)
{
  uint32_t until = millis() + ms;
  while (millis() < until)
  {
    BtnEvent ev = check();
    if (ev != BTN_NONE) return ev;
    delay(10);
  }
  return BTN_NONE;
}
