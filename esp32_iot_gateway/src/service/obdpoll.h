#pragma once
#include "../domain/obd.h"

// 全PID逐次問い合わせ（canInit() 済み前提）。ログ出力・OLED表示にのみ使う（AWSへの送信は今回のスコープ外）
OBDReading obdPoll();

// デバッグ用（DEBUG_FAKE_OBD_DATA、config.h参照）: CANバス未接続でも固定値のダミー
// OBDReadingをvalid=trueで返す。全フィールドに現実的な値（アイドリング相当）を入れる。
// 実車のOBD2ケーブル無しでcar_iot_android側のパーサーを検証するためのもの。
OBDReading obdPollFake();

// PID 0x0C（RPM、全車必須サポート）のみを問い合わせ、応答の有無だけを返す軽量版。
// CAN応答の値は使わずCONTINUOUS昇格判定（IGN ON相当）にのみ使うため、obdPoll()の
// 29PID+DID調査をまるごと送る必要はない（mode_common.cpp:detectContinuousPromotionTrigger()参照）
bool obdCheckCanAlive();
