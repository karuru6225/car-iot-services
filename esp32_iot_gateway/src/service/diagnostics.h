#pragma once
#include <esp_sleep.h>

// 起動直後に呼ぶ。起動回数のカウントとリセット理由・wakeup要因の記録を行う
void diagnosticsInit(esp_sleep_wakeup_cause_t wakeupCause);

// 現在の診断情報（t:"diag"）をオフラインキューに積む。loop()の1サイクルにつき1回呼ぶ想定。
// LTE接続中に呼ぶこと（AT+CSQを発行するため）
void publishDiagnostics();
