#pragma once

#include <Arduino.h>
#include "../domain/measurement.h"

void oledInit();
void oledPrint(const char *text);
void oledClear();
void oledShowStatus(float voltage, float voltage2, bool relayOn, bool btn0, bool btn1);
void oledShowOtaProgress(const char *stage, size_t current, size_t total);

// メニュー用
void oledShowMenu(const char *title, const char *items[], int count, int cursor);
void oledShowMessage(const char *line1, const char *line2 = nullptr);
void oledShowConfirm(const char *message, const char *item, int yesNoCursor);
void oledShowSensorData(const SensorReading &reading);
void oledUpdateCountdown(int remainSec); // 計測値画面の下部だけ更新（継続モード用）
