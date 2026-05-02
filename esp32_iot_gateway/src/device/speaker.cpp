#include "speaker.h"

#define BUZZER_PIN 10

// 音符周波数
#define C4 262
#define D4 294
#define E4 330
#define F4 349
#define G4 392
#define A4 440
#define B4 494
#define C5 523

typedef struct Note
{
  int freq;
  int dur;
} Note;

// きらきら星
const Note melody[] = {
    {1000, 100},
    {1500, 100},
    {2000, 100},
    // {C4, 400},
    // {C4, 400},
    // {G4, 400},
    // {G4, 400},
    // {A4, 400},
    // {A4, 400},
    // {G4, 800},
    // {F4, 400},
    // {F4, 400},
    // {E4, 400},
    // {E4, 400},
    // {D4, 400},
    // {D4, 400},
    // {C4, 800},
    // {G4, 400},
    // {G4, 400},
    // {F4, 400},
    // {F4, 400},
    // {E4, 400},
    // {E4, 400},
    // {D4, 800},
    // {G4, 400},
    // {G4, 400},
    // {F4, 400},
    // {F4, 400},
    // {E4, 400},
    // {E4, 400},
    // {D4, 800},
    // {C4, 400},
    // {C4, 400},
    // {G4, 400},
    // {G4, 400},
    // {A4, 400},
    // {A4, 400},
    // {G4, 800},
    // {F4, 400},
    // {F4, 400},
    // {E4, 400},
    // {E4, 400},
    // {D4, 400},
    // {D4, 400},
    // {C4, 800},
};

void playMelody(uint8_t pin)
{

  pinMode(pin, OUTPUT);
  for (auto &n : melody)
  {
    tone(pin, n.freq, n.dur * 0.9); // 音符の90%を音、残り10%は無音（スタッカート）
    delay(n.dur);
  }
  noTone(pin);
}