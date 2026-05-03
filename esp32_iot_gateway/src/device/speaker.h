#pragma once

#include <Arduino.h>
#include <time.h>

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

// 起動開始（単音）
const Note bootStart[] = {
    {C5, 120},
    {0, 0},
};

// 起動完了（3音上昇）
const Note boot[] = {
    {1000, 100},
    {1500, 100},
    {2000, 100},
    {0, 0},
};

void speakerInit();
void playMelody(const Note *melody);