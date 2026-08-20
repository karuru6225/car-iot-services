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

const DidScanPreset kDidScanPresets[] = {
    // OBD.md「Mode 22 実機テスト候補」記載の候補DID周辺（優先領域、先頭に配置）
    {"ATF near 0x22xx", 0x2200, 0x22FF},
    {"Coolant cand 0x11xx", 0x1100, 0x11FF},
    {"Coolant cand 0x40xx", 0x4000, 0x40FF},
    // 全域を0x1000刻みで16分割
    {"0x0000-0x0FFF", 0x0000, 0x0FFF},
    {"0x1000-0x1FFF", 0x1000, 0x1FFF},
    {"0x2000-0x2FFF", 0x2000, 0x2FFF},
    {"0x3000-0x3FFF", 0x3000, 0x3FFF},
    {"0x4000-0x4FFF", 0x4000, 0x4FFF},
    {"0x5000-0x5FFF", 0x5000, 0x5FFF},
    {"0x6000-0x6FFF", 0x6000, 0x6FFF},
    {"0x7000-0x7FFF", 0x7000, 0x7FFF},
    {"0x8000-0x8FFF", 0x8000, 0x8FFF},
    {"0x9000-0x9FFF", 0x9000, 0x9FFF},
    {"0xA000-0xAFFF", 0xA000, 0xAFFF},
    {"0xB000-0xBFFF", 0xB000, 0xBFFF},
    {"0xC000-0xCFFF", 0xC000, 0xCFFF},
    {"0xD000-0xDFFF", 0xD000, 0xDFFF},
    {"0xE000-0xEFFF", 0xE000, 0xEFFF},
    {"0xF000-0xFFFF", 0xF000, 0xFFFF},
};
const int kDidScanPresetCount = sizeof(kDidScanPresets) / sizeof(kDidScanPresets[0]);

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
      logger.printf("[DIDScan] ヒット(NRC 0x%02X、存在確認のみ) DID=0x%04X\n", nrc, did);
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
