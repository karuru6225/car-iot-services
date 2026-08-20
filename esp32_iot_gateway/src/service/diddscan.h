#pragma once
#include <stdint.h>

// Mode 22 (UDS ReadDataByIdentifier) の DID 総当たりスキャン。
// 燃料残量・油温等、Mode 01 非対応データの独自 PID を探すための一時的な調査機能
// （OBD.md「Mode 22 PID 探索方法論」参照）。canInit() 済み前提、通常運用のポーリングには含めない。

// 1件のヒット（正常応答、またはNRC 0x22/0x33で「存在するが今は読めない」判定されたDID）
struct DidScanFinding
{
  uint16_t did;
  bool     ok;         // true: 正常応答(62応答) / false: NRCヒット（存在確認のみ）
  uint8_t  nrc;         // ok=falseの場合のNRC値
  uint8_t  respLen;      // ok=trueの場合の応答データ長（PCI剥離後、62 DID_HI DID_LO含む）
  uint8_t  respData[8]; // ok=trueの場合の応答データ先頭
};

struct DidScanResult
{
  static const int MAX_FINDINGS = 24; // OLED一覧表示の都合上、多すぎる場合は先着順で打ち切る
  DidScanFinding findings[MAX_FINDINGS];
  int  findingCount;
  int  scannedCount; // 実際に問い合わせたDID数（中断時はtotalCountより少ない）
  int  totalCount;   // 範囲内の全DID数
  bool aborted;       // shouldAbort()がtrueを返して中断した場合true
};

// [start, end]（inclusive）をDID総当たりスキャンする。1件ごとにshouldAbort()を呼び、
// trueが返れば即座に打ち切る（メニュー側でボタン押下を監視できるようにするコールバック）。
// 正常応答（Ok）とNRC 0x22(conditionsNotCorrect)/0x33(securityAccessDenied)のみ
// resultに記録する。NRC 0x31(requestOutOfRange)非対応・タイムアウトは件数カウントのみ。
void didScanRun(uint16_t start, uint16_t end, DidScanResult &result, bool (*shouldAbort)());
