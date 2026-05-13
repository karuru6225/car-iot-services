#pragma once

#include <Arduino.h>

bool adsInit();
float adsReadDiffMain(); // AIN0/AIN1 = メインバッテリー（J104）
float adsReadDiffSub();  // AIN2/AIN3 = サブバッテリー（J103）