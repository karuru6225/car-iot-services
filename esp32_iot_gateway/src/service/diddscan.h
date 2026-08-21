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

// 全域スキャン（2026-08-20実施）で正常応答が確認済みのDID一覧。値の意味は未確定
// （OBD.md「DIDスキャン結果」参照）。didReadCandidateValues()の対象。
extern const uint16_t kDidCandidates[];
extern const int kDidCandidateCount;

// 1件の実値読み取り結果
struct DidValueReading
{
  uint16_t did;
  bool     ok;        // 応答が得られたか
  uint8_t  len;        // ペイロード長（62 DID_HI DID_LO を除いた分）
  uint8_t  data[8];    // ペイロード先頭
};

struct DidValueResult
{
  static const int MAX_ITEMS = 16; // kDidCandidateCount以上であること
  DidValueReading items[MAX_ITEMS];
  int count;
};

// kDidCandidates[]を1件ずつ22XXYYで問い合わせ、ペイロードをresultに格納しつつ
// シリアルログにも出力する（燃料補給前後・冷間/暖機後の値変化を突き合わせる調査用）。
void didReadCandidateValues(DidValueResult &result);
