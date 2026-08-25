#pragma once

// PCとのUSBシリアル経由の生CANパススルー（プロキシ）モード。ISO-TP/UDSのフレーミングを
// 一切介さず、任意のCANフレームをPC⇔ECU間でそのまま中継する調査用機能
// （OBD.md「CAN Proxyモード」参照）。canInit()済み前提。
//
// プロトコル（固定長バイナリ、いずれもSerial 115200bps）:
//   PC→ESP32（送信要求）: 0x55 <FLAGS:1B> <ID:4B LE> <DLC:1B> <DATA:0-8B> <CHECKSUM:1B>
//   ESP32→PC（受信転送）: 0xAA <FLAGS:1B> <ID:4B LE> <DLC:1B> <DATA:0-8B> <CHECKSUM:1B>
//   FLAGS bit0: 1=29bit拡張ID / 0=11bit標準ID
//   CHECKSUM: FLAGS/ID/DLC/DATA全バイトのXOR。0x55/0xAAへの偶然の一致でゴミデータが
//   フレームとして誤認識されるのを防ぐ（特にPC→ESP32方向は不一致ならCAN送信自体を行わない）
//
// このモード中はUSBシリアルを上記バイナリプロトコル専用にする必要があるため、
// logger（通常のデバッグprint）は一切呼ばない。shouldExit()がtrueを返すまでブロッキングで
// 動作し続ける（BTN1長押しでの中断はmenu.cpp側のコールバックで実装する想定）。
void canProxyRun(bool (*shouldExit)());
