#pragma once

// PCとのUSBシリアル経由の生CANパススルー（プロキシ）モード。ISO-TP/UDSのフレーミングを
// 一切介さず、任意のCANフレームをPC⇔ECU間でそのまま中継する調査用機能
// （OBD.md「CAN Proxyモード」参照）。
//
// SLCAN(LAWICEL)プロトコル互換で実装している（ASCIIテキスト、'\r'終端の行指向コマンド）。
// SavvyCAN・python-can(slcanインターフェース)・Linux slcand等の既存ツールがそのまま接続できる。
// 仕様はhttp://www.can232.com/docs/canusb_manual.pdf を参照して独自に実装した（コードの流用なし）。
//
// 主なコマンド:
//   O          CANを開く（内部でcanInit()を呼ぶ）
//   C          CANを閉じる（内部でcanDeinit()を呼ぶ）
//   t<ID3><L><DATA...>  標準(11bit)データフレーム送信
//   T<ID8><L><DATA...>  拡張(29bit)データフレーム送信
//   r<ID3><L>           標準RTRフレーム送信
//   R<ID8><L>           拡張RTRフレーム送信
//   S6         ビットレート確認（このボードは500kbps固定のためS6のみACK、他はNACK）
//   Z0/Z1      受信通知への4桁hexタイムスタンプ付与 OFF/ON
// 受信したCANフレームは同じt/T/r/R形式で非同期にPCへ通知する（'O'済みの間のみ）。
// 各コマンドへの応答はACK='\r'、NACK='\a'(BEL)。
//
// shouldExit()がtrueを返すまでブロッキングで動作し続ける
// （BTN1長押しでの中断はmenu.cpp側のコールバックで実装する想定）。
// 終了時、CANが開いたままなら内部でcanDeinit()する。
void canProxyRun(bool (*shouldExit)());
