#pragma once
#include <stdint.h>
#include <optional>
#include "../config.h"

// Shadow reported に現在の設定値を publish する
// clearDesired=true のとき desired:null も付加して desired をクリアする
void shadowPublishConfig(bool clearDesired = false);

// Shadow delta トピックを subscribe する（setup 時に 1 回呼ぶ）
void shadowSetup();

// delta を受信して設定に適用する（受信した場合 true を返す）
bool shadowPollDelta(uint32_t timeoutMs = 2000);

// Shadow の override_next_mode delta から要求されたモードを返す（読んだらリセット）
// オーバーライドなし: nullopt
std::optional<OperationMode> getShadowOverrideMode();
