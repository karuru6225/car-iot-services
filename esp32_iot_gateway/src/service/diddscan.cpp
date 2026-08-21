#include "diddscan.h"
#include "../device/can.h"
#include "../logger.h"
#include <string.h>
#include <time.h>

namespace
{
const uint8_t NRC_CONDITIONS_NOT_CORRECT = 0x22;
const uint8_t NRC_SECURITY_ACCESS_DENIED = 0x33;
// 応答なしDIDでの待ち時間を抑えるため通常ポーリング(50ms)より短めに設定
const uint32_t DID_SCAN_TIMEOUT_MS = 30;

// AWS側にアップロードされるOBDデータ（mobile側obd_uploader.dartが送るts=time(nullptr)の秒値）と
// 突き合わせられるよう、読み取り直前の時刻を記録する。未同期（time(nullptr)がNTP/LTE同期前の
// 1970年付近を指す）の場合はlog_storage.cppと同じ閾値でmillis()基準にフォールバックする。
void logCurrentTime(const char *tag)
{
  time_t now = time(nullptr);
  if (now > 1577836800L) // 2020-01-01以降なら同期済みとみなす
  {
    char timebuf[24];
    strftime(timebuf, sizeof(timebuf), "%Y-%m-%dT%H:%M:%SZ", gmtime(&now));
    logger.printf("%s ts=%ld (%s)\n", tag, (long)now, timebuf);
  }
  else
  {
    logger.printf("%s ts=未同期 millis=%lu\n", tag, millis());
  }
}
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

const uint16_t kDidCandidates[] = {
    0x2341, 0x2342, 0x2601, 0x2630, 0xE5FF, 0xE600, 0xE602, 0xF100, 0xF806,
};
const int kDidCandidateCount = sizeof(kDidCandidates) / sizeof(kDidCandidates[0]);
static_assert(sizeof(kDidCandidates) / sizeof(kDidCandidates[0]) <= DidValueResult::MAX_ITEMS,
              "kDidCandidatesがDidValueResult::MAX_ITEMSを超えている");

void didReadCandidateValues(DidValueResult &result)
{
  result = {};
  logCurrentTime("[DIDVal] 開始");

  uint8_t data[8];
  uint8_t dlc;

  for (int i = 0; i < kDidCandidateCount; i++)
  {
    uint16_t did = kDidCandidates[i];
    DidValueReading &item = result.items[result.count++];
    item.did = did;

    if (!canSendObdRequestUds(did))
    {
      logger.printf("[DIDVal] DID=0x%04X 送信失敗\n", did);
      continue;
    }
    if (canReceiveObdResponse(data, &dlc, DID_SCAN_TIMEOUT_MS, sizeof(data)) != ObdRecvResult::Ok)
    {
      logger.printf("[DIDVal] DID=0x%04X 応答なし\n", did);
      continue;
    }

    // dataは[0]=0x62 [1..2]=DID [3..]=ペイロード。62/DIDエコー部分は自明なので
    // ログ・resultにはペイロード（実値）だけを残す。
    item.ok = true;
    item.len = dlc > 3 ? dlc - 3 : 0;
    memcpy(item.data, data + 3, item.len < sizeof(item.data) ? item.len : sizeof(item.data));

    char hex[3 * sizeof(item.data) + 1] = {0};
    for (uint8_t b = 0; b < item.len && b < sizeof(item.data); b++)
      snprintf(hex + b * 3, 4, "%02X ", item.data[b]);
    logger.printf("[DIDVal] DID=0x%04X len=%u data=%s\n", did, item.len, hex);
  }
}
