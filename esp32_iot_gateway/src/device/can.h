#pragma once
#include <stdint.h>

// GU0 コネクタ（gu00Pin=RX, gu01Pin=TX, gu0EnPin=EN）に接続した MCP2562FD 経由で
// Honda N-VAN の OBD-II（Mode 01、29bit 拡張アドレッシング）と通信する。
// 詳細は CAN_TEST.md / CAN_REFERENCE.md / OBD.md 参照。

// CAN 起動（電源 ON → TWAI 500kbps NORMAL 起動）。既に起動済みなら即 true を返す（冪等）
bool canInit();

// CAN 停止（TWAI 停止 → 電源 OFF）。未起動でも安全に呼べる
void canDeinit();

// Mode 01 PID リクエストを送信する
bool canSendObdRequest(uint8_t pid);

// 応答を受信する（29bit: 0x18DAF1xx、11bit フォールバック: 0x7E8）。timeoutMs 内に届かなければ false
// ISO-TP マルチフレーム（First Frame + Consecutive Frame）にも対応し、Flow Controlを自動送信する。
// data には ISO-TP の PCI バイトを除いたペイロード（41 PID data...）を返す。
// maxLen は data バッファの最大サイズ。応答長がこれを超える場合は false を返す（バッファ溢れ防止）
bool canReceiveObdResponse(uint8_t *data, uint8_t *dlc, uint32_t timeoutMs = 100, uint8_t maxLen = 8);

// TWAIコントローラの状態・エラーカウンタ（TEC/REC等）をログ出力する（診断用）
void canLogStatus(const char *tag);
