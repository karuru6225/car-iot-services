#pragma once
#include <stdint.h>

// GU0 コネクタ（gu00Pin=TX, gu01Pin=RX, gu0EnPin=EN）に接続した MCP2562FD 経由で
// Honda N-VAN の OBD-II（Mode 01、29bit 拡張アドレッシング）と通信する。
// 詳細は CAN_TEST.md / CAN_REFERENCE.md / OBD.md 参照。

// CAN 起動（電源 ON → TWAI 500kbps NORMAL 起動）。既に起動済みなら即 true を返す（冪等）
bool canInit();

// CAN 停止（TWAI 停止 → 電源 OFF）。未起動でも安全に呼べる
void canDeinit();

// Mode 01 で複数PIDをまとめて1フレーム(Single Frame)で要求する。count は1〜6
// （PCIバイトの長さ制約: Mode1バイト+PID数がSF7バイト以内に収まる上限）。
// functional addressing固定（実車で2PID要求時の動作を確認済み。HANDOFF_isotp_multipid.md
// §4テスト1参照。物理アドレッシングは未対応）。
bool canSendObdRequestMulti(const uint8_t *pids, uint8_t count);

enum class ObdRecvResult : uint8_t
{
  Ok,              // 正常応答（data/dlcに`41 PID data...`等が入っている）
  Timeout,         // timeoutMs内に一致する応答が来なかった
  NegativeResponse,// 否定応答（`7F [SID] [NRC]`）。nrcOutにNRCを返す
  Error,           // 未起動・不正なフレーム長など、上記以外の失敗
};

// 応答を受信する（29bit: 0x18DAF1xx、11bit フォールバック: 0x7E8）。
// ISO-TP マルチフレーム（First Frame + Consecutive Frame）にも対応し、Flow Controlを自動送信する。
// data には ISO-TP の PCI バイトを除いたペイロード（41 PID data...）を返す。
// maxLen は data バッファの最大サイズ。応答長がこれを超える場合は Error を返す（バッファ溢れ防止）。
// 否定応答（`7F [SID] [NRC]`）を受信した場合は NegativeResponse を返し、nrcOutが非nullptrならNRCを書き込む。
ObdRecvResult canReceiveObdResponse(uint8_t *data, uint8_t *dlc, uint32_t timeoutMs = 100,
                                     uint8_t maxLen = 8, uint8_t *nrcOut = nullptr);

// TWAIコントローラの状態・エラーカウンタ（TEC/REC等）をログ出力する（診断用）
void canLogStatus(const char *tag);
