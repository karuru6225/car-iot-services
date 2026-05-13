#pragma once

// Shadow reported に現在の設定値を publish する
void shadowPublishConfig();

// Shadow delta トピックを subscribe する（setup 時に 1 回呼ぶ）
void shadowSetup();

// delta を受信して設定に適用する（受信した場合 true を返す）
bool shadowPollDelta();
