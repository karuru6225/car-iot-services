#include "speaker.h"
#include "../board_pins.h"

static const uint8_t BUZZER_PIN = boardPins().buzzerPin;

// きらきら星
const Note melody[] = {
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

void speakerInit()
{
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, HIGH); // 負論理
}

void playTone(int freq, int durationMs)
{
  tone(BUZZER_PIN, freq, durationMs);
}

void playMelody(const Note *melody)
{
  // {0, 0} をセンチネルとして末尾を検出する
  for (int i = 0; melody[i].freq != 0; i++)
  {
    Note n = melody[i];
    tone(BUZZER_PIN, n.freq, (unsigned long)(n.dur * 0.9)); // 音符の90%を音、残り10%は無音（スタッカート）
    // delay(n.dur)で一括ブロッキングせず、millis()で経過時間を見ながら短い刻みでポーリングする
    // （continuousLoopCore()の待機ループと同じパターン）
    unsigned long noteStart = millis();
    while (millis() - noteStart < (unsigned long)n.dur)
      delay(1);
  }
  noTone(BUZZER_PIN);
  digitalWrite(BUZZER_PIN, HIGH); // 念のため確実にオフ
}