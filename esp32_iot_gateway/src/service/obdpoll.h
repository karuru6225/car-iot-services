#pragma once
#include "../domain/obd.h"

// 全PID逐次問い合わせ（canInit() 済み前提）。ログ出力・OLED表示にのみ使う（AWSへの送信は今回のスコープ外）
OBDReading obdPoll();
