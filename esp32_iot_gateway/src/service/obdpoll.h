#pragma once
#include "../domain/obd.h"

// 全PID逐次問い合わせ（canInit() 済み前提）。ログ出力・OLED表示にのみ使う（AWSへの送信は今回のスコープ外）
OBDReading obdPoll();

// 6PIDずつバルク送信するポーリング（canInit() 済み前提）。
// ISO 15765-4 Single Frameの制約上1フレームに最大6PID詰められる仕様を利用するが、
// Honda N-VANでの対応は未検証の実験機能（OBD.md参照）
OBDReading obdPollBulk();

// 次の obdPollTick() 呼び出しから指定サイクル数だけバルクポーリングに切り替える
// （Shadowの obd_bulk_test デルタ、またはメニューの "OBD Bulk Test" から呼ばれる）
void requestObdBulkTest(int cycles = 5);

// CONTINUOUS_OBD の1秒ティックから呼ぶラッパー。
// バルクテスト中は obdPollBulk()、それ以外は通常の obdPoll() を呼ぶ
OBDReading obdPollTick();
