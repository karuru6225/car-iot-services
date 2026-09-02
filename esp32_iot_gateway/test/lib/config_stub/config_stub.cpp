// telemetry.cppのbuildConfigPayload()はconfig.hの設定値ゲッターに依存している。
// 実機実装(config.cpp)はNVS(Preferences/ESP-IDF)に依存しnativeでは動かせないため、
// テストから値を差し替えられる固定値スタブをここに用意する。
#include "config.h"

static int32_t s_ahOffset = 0;
static float s_chgStartV = 12.0f;
static float s_chgStopV = 14.0f;
static float s_chgMinDiffV = 0.3f;
static bool s_debugLog = false;
static bool s_charging = false;

int32_t getAhOffset() { return s_ahOffset; }
float getChgStartV() { return s_chgStartV; }
float getChgStopV() { return s_chgStopV; }
float getChgMinDiffV() { return s_chgMinDiffV; }
bool getDebugLogEnabled() { return s_debugLog; }
bool isCharging() { return s_charging; }
void setCharging(bool v) { s_charging = v; }

void testSetAhOffset(int32_t v) { s_ahOffset = v; }
void testSetChgStartV(float v) { s_chgStartV = v; }
void testSetChgStopV(float v) { s_chgStopV = v; }
void testSetChgMinDiffV(float v) { s_chgMinDiffV = v; }
void testSetDebugLogEnabled(bool v) { s_debugLog = v; }
