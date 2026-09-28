#pragma once
#include <stdint.h>

// 診断テレメトリ（t:"diag"）の中身。シリアルにしか出ていなかった「デバイス自身の状態」を
// クラウドに時系列で残し、欠測時に電源断・再起動ループ・圏外・キュー溢れを切り分けるために使う。
//
// pubqueue の QueueEntry union にそのまま載せ、SPIFFS(/buffer.bin)へ生バイト列で保存される。
// union サイズ（=BatteryEntry の28バイト）を超えると既存の保存ファイルと互換が崩れるため、
// フィールドを足すときは pubqueue.h の static_assert が通ることを確認すること。
struct DiagData
{
  uint32_t ts;           // 計測時刻（UNIX時間）
  uint32_t bootCount;    // 起動回数。RTCメモリ保持のため電源投入・ブラウンアウト等で0に戻る
  uint32_t uptimeSec;    // 起動からの経過秒（DeepSleep運用では1サイクルの所要時間に相当）
  uint32_t heapFree;     // 空きヒープ（バイト）
  uint32_t heapMin;      // 起動以降の空きヒープ最小値（バイト）
  uint16_t queueDropped; // オフラインキュー溢れで捨てた件数の累計（RTCメモリ保持）
  uint8_t  resetReason;  // esp_reset_reason() の値（1=POWERON, 4=PANIC, 6=TASK_WDT, 9=BROWNOUT 等）
  uint8_t  wakeupCause;  // esp_sleep_get_wakeup_cause() の値（0=UNDEFINED, 4=TIMER 等）
  uint8_t  csq;          // AT+CSQ の値（0〜31、99=不明）
  uint8_t  queueLen;     // 計測時点のオフラインキュー滞留件数
  uint8_t  mode;         // OperationMode の値
};
