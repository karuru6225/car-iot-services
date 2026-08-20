#include "diddscan.h"
#include "../device/can.h"
#include "../logger.h"
#include <string.h>

namespace
{
const uint8_t NRC_CONDITIONS_NOT_CORRECT = 0x22;
const uint8_t NRC_SECURITY_ACCESS_DENIED = 0x33;
// 応答なしDIDでの待ち時間を抑えるため通常ポーリング(50ms)より短めに設定
const uint32_t DID_SCAN_TIMEOUT_MS = 30;
} // namespace

void didScanRun(uint16_t start, uint16_t end, DidScanResult &result, bool (*shouldAbort)())
{
  result = {};
  result.totalCount = (int)end - (int)start + 1;

  logger.printf("[DIDScan] 開始 0x%04X-0x%04X (%d件)\n", start, end, result.totalCount);

  uint8_t data[8];
  uint8_t dlc;
  uint8_t nrc;
  // ループ変数はuint32_t: endが0xFFFFの場合uint16_tだとインクリメントでオーバーフローし無限ループになる
  for (uint32_t did32 = start; did32 <= (uint32_t)end; did32++)
  {
    if (shouldAbort())
    {
      result.aborted = true;
      break;
    }
    uint16_t did = (uint16_t)did32;
    result.scannedCount++;

    if (!canSendObdRequestUds(did))
      continue;

    ObdRecvResult r = canReceiveObdResponse(data, &dlc, DID_SCAN_TIMEOUT_MS, sizeof(data), &nrc);

    if (r == ObdRecvResult::Ok)
    {
      logger.printf("[DIDScan] ヒット(正常応答) DID=0x%04X dlc=%u\n", did, dlc);
      if (result.findingCount < DidScanResult::MAX_FINDINGS)
      {
        DidScanFinding &f = result.findings[result.findingCount++];
        f.did = did;
        f.ok = true;
        f.respLen = dlc;
        memcpy(f.respData, data, dlc < sizeof(f.respData) ? dlc : sizeof(f.respData));
      }
    }
    else if (r == ObdRecvResult::NegativeResponse &&
             (nrc == NRC_CONDITIONS_NOT_CORRECT || nrc == NRC_SECURITY_ACCESS_DENIED))
    {
      if (result.findingCount < DidScanResult::MAX_FINDINGS)
      {
        DidScanFinding &f = result.findings[result.findingCount++];
        f.did = did;
        f.ok = false;
        f.nrc = nrc;
      }
    }
    // それ以外（NRC 0x31非対応・Timeout・Error）は無視、件数カウントのみ
  }

  logger.printf("[DIDScan] 終了 スキャン=%d/%d ヒット=%d 中断=%d\n",
                result.scannedCount, result.totalCount, result.findingCount, (int)result.aborted);
}
