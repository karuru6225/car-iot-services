#include "diagnostics.h"
#include "pubqueue.h"
#include "mode_context.h"
#include "../logger.h"
#include "../device/lte.h"
#include "../domain/diag.h"
#include <Arduino.h>
#include <esp_system.h>
#include <time.h>

// RTCメモリ保持のためDeepSleepをまたいで増え続け、電源投入・ブラウンアウト等で0に戻る。
// resetReasonと合わせて見ると「いつ・何が原因で再起動したか」を追える
RTC_DATA_ATTR static uint32_t g_bootCount = 0;

static uint8_t s_resetReason = 0;
static uint8_t s_wakeupCause = 0;

void diagnosticsInit(esp_sleep_wakeup_cause_t wakeupCause)
{
  g_bootCount++;
  s_resetReason = (uint8_t)esp_reset_reason();
  s_wakeupCause = (uint8_t)wakeupCause;
  logger.printf("[DIAG] boot=%u reset=%u wakeup=%u\n",
                (unsigned)g_bootCount, s_resetReason, s_wakeupCause);
}

void publishDiagnostics()
{
  // getSignalQuality()は応答パース失敗時に範囲外の値を返しうるため、0〜31以外は99（不明）に丸める
  int csq = lte.signalQuality();
  if (csq < 0 || csq > 31)
    csq = 99;

  DiagData d = {};
  d.ts           = (uint32_t)time(nullptr);
  d.bootCount    = g_bootCount;
  d.uptimeSec    = millis() / 1000;
  d.heapFree     = ESP.getFreeHeap();
  d.heapMin      = ESP.getMinFreeHeap();
  d.queueDropped = queue.droppedCount();
  d.resetReason  = s_resetReason;
  d.wakeupCause  = s_wakeupCause;
  d.csq          = (uint8_t)csq;
  d.queueLen     = (uint8_t)min(queue.size(), 255);
  d.mode         = (uint8_t)modeCtx.mode();

  logger.printf("[DIAG] up=%us heap=%u/%u csq=%u queue=%u dropped=%u\n",
                (unsigned)d.uptimeSec, (unsigned)d.heapFree, (unsigned)d.heapMin,
                d.csq, d.queueLen, (unsigned)d.queueDropped);

  queue.pushDiag(d);
}
