#pragma once

#include <Arduino.h>

void oledInit();
void oledPrint(const char *text);
void oledShowStatus(float voltage, float voltage2, bool relayOn, bool btn0, bool btn1);
