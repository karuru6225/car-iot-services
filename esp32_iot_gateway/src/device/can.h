#pragma once
#include <stdint.h>

// GU0 コネクタ（gu00Pin=TX, gu01Pin=RX, gu0EnPin=EN）に接続した MCP2562FD 経由で
// Honda N-VAN の OBD-II（Mode 01、29bit 拡張アドレッシング）と通信する。
// 詳細は CAN_REFERENCE.md / OBD.md 参照。

// CAN 起動（電源 ON → TWAI 500kbps NORMAL 起動）。既に起動済みなら即 true を返す（冪等）。
// quiet=trueならlogger出力を一切行わない（CAN Proxyモードがslcanの'O'コマンドから呼ぶ用。
// USBシリアルをSLCANプロトコル専用にする必要があるため。service/can_proxy.cpp参照）
bool canInit(bool quiet = false);

// CAN 停止（TWAI 停止 → 電源 OFF）。未起動でも安全に呼べる。
// quiet=trueの用途はcanInit()と同じ（CAN Proxyモードの'C'コマンドから呼ぶ用）
void canDeinit(bool quiet = false);

// Mode 01 で複数PIDをまとめて1フレーム(Single Frame)で要求する。count は1〜6
// （PCIバイトの長さ制約: Mode1バイト+PID数がSF7バイト以内に収まる上限）。
// functional addressing固定（実車で2PID要求時の動作を確認済み。経緯はCONTEXT_ARCHIVE.mdの
// 「ISO-TPマルチフレーム対応・多PID要求」参照。物理アドレッシングは未対応）。
bool canSendObdRequestMulti(const uint8_t *pids, uint8_t count);

// Mode 22 (UDS ReadDataByIdentifier) でDIDを1件要求する。物理アドレッシング
// （0x18DA<ecuAddr>F1）。ecuAddrの既定値0x0Eはエンジンユニット。Mode01の機能アドレッシングとは
// 異なる（経緯はOBD.md「Mode 22 実機テスト候補」参照）。
// ecuAddr: 対象ECUアドレス（既定0x0E=エンジンECU。0x1E=TCM候補等、canScanEcuAddresses()の
// 結果を渡す想定。OBD.md「DID 0x2201が実車で一度も成功していない問題」参照）。
bool canSendObdRequestUds(uint16_t did, uint8_t ecuAddr = 0x0E);

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

// 機能アドレッシング(0x18DB33F1)でUDS DiagnosticSessionControl(10 01)をブロードキャストし、
// windowMs間に届いた応答ECUアドレス（応答ID 0x18DAF1xxの下位バイト）を重複なく収集する。
// canSendObdRequestUds()はエンジンECU(0x0E)固定のため、それ以外にOBD-IIポート経由で
// UDSサービスに応答するECU（トランスミッション制御ユニット等）が存在するかを調べる調査用
// （OBD.md「ATF温度が一度も取得できない」問題の切り分け参照）。
// ecuAddrsOutにはmaxCount件まで書き込み、実際に見つかった件数を返す（0=応答なし）。
uint8_t canScanEcuAddresses(uint8_t *ecuAddrsOut, uint8_t maxCount, uint32_t windowMs = 300);

// ---- CAN Proxyモード用: 生フレームの送受信（ISO-TP/UDSのフレーミングを一切介さない） ----
// service/can_proxy.cpp専用。USBシリアルをCAN Proxyのバイナリプロトコル専用にするため、
// この2関数はログ出力・バスオフ自動復帰を一切行わない（recoverIfBusOff()も呼ばない）。
// バスオフ復帰が必要な場合はプロキシモードを抜けて他のOBD機能を使えば通常通り復帰する。
// canInit()済み前提（OBD.md「CAN Proxyモード」参照）。

// 任意のCANフレームを1件送信する。dlcは0〜8、それ以外はfalse。
// rtr=trueならRTRフレーム（データなし、dlcは要求フレーム長の意味のみ）として送信する
bool canRawTransmit(uint32_t id, bool extd, const uint8_t *data, uint8_t dlc, bool rtr = false);

// timeoutMs以内に受信したCANフレームを1件返す（IDのフィルタなし、全フレーム対象）。
// 受信できなければfalseを返し、引数は変更しない。RTRフレームの場合dataは書き込まない
bool canRawReceive(uint32_t *id, bool *extd, bool *rtr, uint8_t *data, uint8_t *dlc, uint32_t timeoutMs);
