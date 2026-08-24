#include "can.h"
#include <Arduino.h>
#include <driver/twai.h>
#include <string.h>
#include "../board_pins.h"
#include "../logger.h"

static const uint8_t CAN_RX_PIN = boardPins().gu01Pin;  // GPIO5: MCP2562FD RXD 側
static const uint8_t CAN_TX_PIN = boardPins().gu00Pin;  // GPIO4: MCP2562FD TXD 側
static const uint8_t CAN_EN_PIN = boardPins().gu0EnPin; // GPIO6: AO3401A ゲート（HIGH=電源ON）

static const uint32_t CAN_REQ_ID = 0x18DB33F1;    // 29-bit functional addressing
static const uint32_t CAN_UDS_REQ_ID = 0x18DA0EF1; // 29-bit physical addressing（対象ECU=0x0E固定、Mode22用）
static const uint32_t CAN_RESP_MASK = 0x18DAF100; // 応答IDの上位24bit（下位8bit=ECUアドレス）
static const uint32_t CAN_TESTER_ADDR = 0xF1;      // 自分（テスター）のアドレス
static const uint32_t CAN_FC_ID_BASE = 0x18DA0000; // FC宛先 = BASE | (ECUアドレス<<8) | CAN_TESTER_ADDR

// ISO-TP (ISO 15765-2) PCI種別（data[0]の上位ニブル）
static const uint8_t ISO_TP_PCI_SF = 0x00; // Single Frame
static const uint8_t ISO_TP_PCI_FF = 0x10; // First Frame
static const uint8_t ISO_TP_PCI_CF = 0x20; // Consecutive Frame
static const uint8_t ISO_TP_PCI_FC = 0x30; // Flow Control

// Flow Control の FlowStatus（PCIバイト下位ニブル）
static const uint8_t ISO_TP_FS_CTS = 0x00;      // Clear To Send
static const uint8_t ISO_TP_FS_OVERFLOW = 0x02; // Overflow（受信側バッファ不足、送信側は即打ち切り）

// FF受信後、CFが届かない場合に打ち切るまでの間隔
static const uint32_t ISO_TP_CF_TIMEOUT_MS = 50;

// バスオフから連続で復帰できない場合にフル再初期化へエスカレーションする閾値
static const uint32_t CAN_FAIL_ESCALATE_THRESHOLD = 20;

static bool s_ready = false;
static uint32_t s_failCount = 0;

// TWAIコントローラの状態・エラーカウンタをまとめてログ出力（診断用）
void canLogStatus(const char *tag)
{
  twai_status_info_t sts;
  if (twai_get_status_info(&sts) != ESP_OK)
  {
    logger.printf("[CAN] %s: twai_get_status_info 失敗\n", tag);
    return;
  }
  const char *stateStr =
      sts.state == TWAI_STATE_RUNNING ? "RUNNING" : sts.state == TWAI_STATE_BUS_OFF ? "BUS_OFF"
                                                 : sts.state == TWAI_STATE_RECOVERING ? "RECOVERING"
                                                                                      : "STOPPED";
  logger.printf("[CAN] %s: state=%s TEC=%u REC=%u txQ=%u rxQ=%u txFail=%u rxMiss=%u rxOverrun=%u arbLost=%u busErr=%u\n",
                tag, stateStr, sts.tx_error_counter, sts.rx_error_counter,
                sts.msgs_to_tx, sts.msgs_to_rx, sts.tx_failed_count,
                sts.rx_missed_count, sts.rx_overrun_count, sts.arb_lost_count, sts.bus_error_count);
}

bool canInit()
{
  if (s_ready)
    return true;

  logger.printf("[CAN] canInit: 開始 TX=GPIO%u RX=GPIO%u EN=GPIO%u\n", CAN_TX_PIN, CAN_RX_PIN, CAN_EN_PIN);

  pinMode(CAN_EN_PIN, OUTPUT);
  digitalWrite(CAN_EN_PIN, HIGH);
  delay(50);

  twai_general_config_t gConfig = TWAI_GENERAL_CONFIG_DEFAULT(
      (gpio_num_t)CAN_TX_PIN, (gpio_num_t)CAN_RX_PIN, TWAI_MODE_NORMAL);
  twai_timing_config_t tConfig = TWAI_TIMING_CONFIG_500KBITS();
  twai_filter_config_t fConfig = TWAI_FILTER_CONFIG_ACCEPT_ALL();

  esp_err_t err = twai_driver_install(&gConfig, &tConfig, &fConfig);
  if (err != ESP_OK)
  {
    logger.printf("[CAN] canInit: twai_driver_install 失敗 (%s)\n", esp_err_to_name(err));
    digitalWrite(CAN_EN_PIN, LOW);
    return false;
  }
  err = twai_start();
  if (err != ESP_OK)
  {
    logger.printf("[CAN] canInit: twai_start 失敗 (%s)\n", esp_err_to_name(err));
    twai_driver_uninstall();
    digitalWrite(CAN_EN_PIN, LOW);
    return false;
  }

  s_ready = true;
  s_failCount = 0;
  logger.println("[CAN] canInit: 起動完了（500kbps NORMAL）");
  canLogStatus("canInit直後");
  return true;
}

void canDeinit()
{
  if (s_ready)
  {
    canLogStatus("canDeinit直前");
    twai_stop();
    twai_driver_uninstall();
    s_ready = false;
    logger.println("[CAN] canDeinit: 停止");
  }
  pinMode(CAN_EN_PIN, OUTPUT);
  digitalWrite(CAN_EN_PIN, LOW);
}

// バスオフ状態なら復帰要求のみ行う（ドライバ再インストールは伴わない軽量処理）
static void recoverIfBusOff()
{
  twai_status_info_t sts;
  if (twai_get_status_info(&sts) != ESP_OK)
  {
    logger.println("[CAN] recoverIfBusOff: twai_get_status_info 失敗");
    return;
  }
  if (sts.state == TWAI_STATE_BUS_OFF)
  {
    canLogStatus("バスオフ検出");
    logger.println("[CAN] バスオフ検出 → 復帰要求");
    esp_err_t err = twai_initiate_recovery();
    if (err != ESP_OK)
      logger.printf("[CAN] twai_initiate_recovery 失敗 (%s)\n", esp_err_to_name(err));
  }
  else if (sts.state != TWAI_STATE_RUNNING)
  {
    logger.printf("[CAN] recoverIfBusOff: 想定外の状態 state=%d\n", (int)sts.state);
  }
}

// SFフレーム送信の共通処理（送信失敗時のログ・連続失敗エスカレーションを共有）
static bool transmitObdRequest(const twai_message_t &tx, const char *logTag)
{
  esp_err_t txErr = twai_transmit(&tx, pdMS_TO_TICKS(10));
  bool ok = txErr == ESP_OK;
  if (!ok)
  {
    logger.printf("[CAN] %s: 送信失敗 err=%s (連続%u回)\n", logTag, esp_err_to_name(txErr), s_failCount + 1);
    if (++s_failCount >= CAN_FAIL_ESCALATE_THRESHOLD)
    {
      logger.printf("[CAN] 連続送信失敗%u回 → フル再初期化\n", s_failCount);
      canLogStatus("再初期化前");
      canDeinit();
      canInit();
    }
  }
  else
  {
    s_failCount = 0;
  }
  return ok;
}

bool canSendObdRequestMulti(const uint8_t *pids, uint8_t count)
{
  if (!s_ready || count == 0 || count > 6)
    return false;

  recoverIfBusOff();

  twai_message_t tx = {};
  tx.identifier = CAN_REQ_ID;
  tx.extd = 1;
  tx.data_length_code = 8;
  tx.data[0] = 1 + count; // PCI: Single Frame, length = Mode1(1) + PID数
  tx.data[1] = 0x01;      // Mode 01
  for (uint8_t i = 0; i < count; i++)
    tx.data[2 + i] = pids[i];
  // 残り（data[2+count..7]）は0x00（ISO 15765-4 パディング、txはゼロ初期化済み）

  return transmitObdRequest(tx, "canSendObdRequestMulti");
}

bool canSendObdRequestUds(uint16_t did)
{
  if (!s_ready)
    return false;

  recoverIfBusOff();

  twai_message_t tx = {};
  tx.identifier = CAN_UDS_REQ_ID; // 物理アドレッシング（Mode01の機能アドレッシングとは異なる）
  tx.extd = 1;
  tx.data_length_code = 8;
  tx.data[0] = 0x03; // PCI: Single Frame, length = SID(1) + DID(2)
  tx.data[1] = 0x22; // Mode 22 (UDS ReadDataByIdentifier)
  tx.data[2] = (uint8_t)(did >> 8);
  tx.data[3] = (uint8_t)(did & 0xFF);
  // 残り（data[4..7]）は0x00パディング

  return transmitObdRequest(tx, "canSendObdRequestUds");
}

// ECUからのOBD応答フレームか判定する（29bit: 0x18DAF1xx、11bit フォールバック: 0x7E8）
static bool isObdResponseFrame(const twai_message_t &rx)
{
  bool is29bit = rx.extd && (rx.identifier & 0xFFFFFF00) == CAN_RESP_MASK;
  bool is11bit = !rx.extd && rx.identifier == 0x7E8; // フォールバック
  return is29bit || is11bit;
}

uint8_t canScanEcuAddresses(uint8_t *ecuAddrsOut, uint8_t maxCount, uint32_t windowMs)
{
  if (!s_ready || maxCount == 0)
    return 0;

  recoverIfBusOff();

  twai_message_t tx = {};
  tx.identifier = CAN_REQ_ID; // 機能アドレッシング（0x18DB33F1、ブロードキャスト）
  tx.extd = 1;
  tx.data_length_code = 8;
  tx.data[0] = 0x02; // PCI: Single Frame, length = SID(1) + subfunction(1)
  tx.data[1] = 0x10; // DiagnosticSessionControl
  tx.data[2] = 0x01; // defaultSession
  // 残り（data[3..7]）は0x00パディング

  if (!transmitObdRequest(tx, "canScanEcuAddresses"))
    return 0;

  uint8_t count = 0;
  twai_message_t rx = {};
  unsigned long deadline = millis() + windowMs;
  while ((int32_t)(millis() - deadline) < 0)
  {
    if (twai_receive(&rx, pdMS_TO_TICKS(10)) != ESP_OK)
      continue;
    if (!rx.extd || !isObdResponseFrame(rx))
      continue; // 11bitフォールバックはECUアドレス抽出の対象外（29bit応答のみ扱う）

    uint8_t ecuAddr = (uint8_t)(rx.identifier & 0xFF);
    bool dup = false;
    for (uint8_t i = 0; i < count; i++)
    {
      if (ecuAddrsOut[i] == ecuAddr)
      {
        dup = true;
        break;
      }
    }
    if (!dup && count < maxCount)
    {
      ecuAddrsOut[count++] = ecuAddr;
      logger.printf("[CAN] ECUスキャン: アドレス0x%02X から応答 id=0x%08X data[0..2]=%02X %02X %02X\n",
                    ecuAddr, (unsigned)rx.identifier, rx.data[0], rx.data[1], rx.data[2]);
    }
  }
  return count;
}

// First Frame受信直後にFlow Controlを送信する（既定はCTS, BS=0, STmin=0固定）。
// 宛先IDは受信したFFの応答元ID（0x18DAF1xx）の下位バイト＝ECUアドレスから組む。
// flowStatus=ISO_TP_FS_OVERFLOWを渡すとECUに即座の送信打ち切りを要求できる
// （バッファ超過でCFを受信できない場合。指定しなければECUはN_Bsタイムアウト＝規格上1秒まで
// 送信状態を保持し続ける）。
static bool sendFlowControl(uint32_t respId, uint8_t flowStatus = ISO_TP_FS_CTS)
{
  uint32_t ecuAddr = respId & 0xFF;
  twai_message_t fc = {};
  fc.identifier = CAN_FC_ID_BASE | (ecuAddr << 8) | CAN_TESTER_ADDR;
  fc.extd = 1;
  fc.data_length_code = 8;
  fc.data[0] = ISO_TP_PCI_FC | (flowStatus & 0x0F);
  fc.data[1] = 0x00; // BlockSize=0（全フレーム連続送信可）
  fc.data[2] = 0x00; // STmin=0
  // data[3..7] = 0x00（パディング）

  esp_err_t err = twai_transmit(&fc, pdMS_TO_TICKS(10));
  if (err != ESP_OK)
    logger.printf("[CAN] ISO-TP: FC送信失敗 id=0x%08X err=%s\n", (unsigned)fc.identifier, esp_err_to_name(err));
  return err == ESP_OK;
}

// First Frame受信後、Consecutive Frameを組み立てて data に追記していく。
// data には既にFF由来の先頭6バイトが書き込み済み（received=6）である前提。
// senderId: FFを送ってきたECUの応答ID（rx.identifier）。機能アドレッシングは複数ECUが
// 応答しうるため、CFはこのIDに一致するものだけを採用する（別ECUのCF混入を防ぐ）。
static bool receiveConsecutiveFrames(uint8_t *data, uint16_t totalLen, uint16_t received, uint32_t senderId)
{
  uint8_t expectedSeq = 1;
  twai_message_t rx = {};
  unsigned long cfDeadline = millis() + ISO_TP_CF_TIMEOUT_MS;
  while (received < totalLen)
  {
    if ((int32_t)(millis() - cfDeadline) > 0)
    {
      logger.println("[CAN] ISO-TP: CF受信タイムアウト");
      return false;
    }
    if (twai_receive(&rx, pdMS_TO_TICKS(10)) != ESP_OK)
      continue;
    if (!isObdResponseFrame(rx) || (rx.data[0] & 0xF0) != ISO_TP_PCI_CF)
      continue; // 無関係フレーム・想定外PCIは無視して待ち続ける
    if (rx.identifier != senderId)
      continue; // FFを送ってきたECUと異なるECUからのCFは無視

    uint8_t seq = rx.data[0] & 0x0F;
    if (seq != (expectedSeq & 0x0F))
    {
      logger.printf("[CAN] ISO-TP: CFシーケンス不一致 期待=%u 受信=%u\n", expectedSeq & 0x0F, seq);
      return false;
    }
    uint16_t remain = totalLen - received;
    uint16_t chunk = remain < 7 ? remain : 7;
    memcpy(data + received, rx.data + 1, chunk);
    received += chunk;
    expectedSeq++;
    cfDeadline = millis() + ISO_TP_CF_TIMEOUT_MS; // CF受信毎にタイムアウトをリセット
  }
  return true;
}

ObdRecvResult canReceiveObdResponse(uint8_t *data, uint8_t *dlc, uint32_t timeoutMs, uint8_t maxLen, uint8_t *nrcOut)
{
  if (!s_ready)
    return ObdRecvResult::Error;

  twai_message_t rx = {};
  unsigned long deadline = millis() + timeoutMs;
  uint32_t unmatchedCount = 0;
  while ((int32_t)(millis() - deadline) < 0)
  {
    if (twai_receive(&rx, pdMS_TO_TICKS(10)) != ESP_OK)
      continue;

    if (!isObdResponseFrame(rx))
    {
      // 想定外ID（別ECU応答やバス上の他フレーム）を受信 → 診断用に記録
      unmatchedCount++;
      logger.printf("[CAN] 未一致フレーム受信 id=0x%08X extd=%d dlc=%u\n",
                    (unsigned)rx.identifier, rx.extd, rx.data_length_code);
      continue;
    }

    uint8_t pciType = rx.data[0] & 0xF0;

    if (pciType == ISO_TP_PCI_SF)
    {
      uint8_t len = rx.data[0] & 0x0F;
      if (len == 0 || len > 7)
      {
        logger.printf("[CAN] ISO-TP: 不正なSF長%u（1-7の範囲外）\n", len);
        return ObdRecvResult::Error;
      }
      if (len > maxLen)
      {
        logger.printf("[CAN] ISO-TP: SF長%uがバッファ上限%uを超過\n", len, maxLen);
        return ObdRecvResult::Error;
      }
      // PCIバイト（1バイト目）を剥がし、ペイロード（41 PID data...）のみを返す
      memcpy(data, rx.data + 1, len);
      *dlc = len;
      // 否定応答: `7F [SID] [NRC]`（常にSFで収まる3バイト）
      if (len >= 3 && data[0] == 0x7F)
      {
        logger.printf("[CAN] ISO-TP: 否定応答受信 SID=0x%02X NRC=0x%02X\n", data[1], data[2]);
        if (nrcOut)
          *nrcOut = data[2];
        return ObdRecvResult::NegativeResponse;
      }
      return ObdRecvResult::Ok;
    }

    if (pciType == ISO_TP_PCI_FF)
    {
      if (!rx.extd)
      {
        logger.println("[CAN] ISO-TP: 11bitフォールバックでのマルチフレームは非対応");
        continue;
      }
      uint16_t totalLen = ((uint16_t)(rx.data[0] & 0x0F) << 8) | rx.data[1];
      if (totalLen < 8)
      {
        logger.printf("[CAN] ISO-TP: 不正なFF長%u（8バイト未満はSFで送られるはず）\n", totalLen);
        return ObdRecvResult::Error;
      }
      if (totalLen > maxLen)
      {
        logger.printf("[CAN] ISO-TP: 応答長%uがバッファ上限%uを超過\n", totalLen, maxLen);
        // FS=Overflowを送り、ECUがN_Bsタイムアウト（規格上1秒）を待たず即座に打ち切れるようにする
        sendFlowControl(rx.identifier, ISO_TP_FS_OVERFLOW);
        return ObdRecvResult::Error;
      }
      memcpy(data, rx.data + 2, 6);

      if (!sendFlowControl(rx.identifier))
        return ObdRecvResult::Error;
      if (!receiveConsecutiveFrames(data, totalLen, 6, rx.identifier))
        return ObdRecvResult::Error;

      *dlc = (uint8_t)totalLen;
      return ObdRecvResult::Ok;
    }

    // CF/FC等、FFに先行されないマルチフレーム断片は想定外 → 無視して待ち続ける
    logger.printf("[CAN] ISO-TP: 想定外PCI 0x%02X を受信（無視）\n", pciType);
  }
  if (unmatchedCount > 0)
    logger.printf("[CAN] canReceiveObdResponse: タイムアウト（未一致%u件受信、一致なし）\n", unmatchedCount);
  return ObdRecvResult::Timeout;
}
