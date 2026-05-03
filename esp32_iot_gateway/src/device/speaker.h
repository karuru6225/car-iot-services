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

const Note boot[] = {
    {C4 * 4, 80},
    {F4 * 4, 80},
    {A4 * 4, 80},
    {C5 * 4, 80},
    {0, 0},
};

void speakerInit();
void playMelody(const Note *melody);
void playTone(int freq, int durationMs); // ノンブロッキング単音