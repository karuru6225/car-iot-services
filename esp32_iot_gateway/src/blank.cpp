#include <Arduino.h>
#include "driver/twai.h"

#define CAN_RX_PIN 8 // MCP2562FD RXD
#define CAN_TX_PIN 7 // MCP2562FD TXD
#define CAN_EN_PIN 9 // AO3401A パワースイッチ: HIGH=電源ON
#define BTN0_PIN 26
#define BTN1_PIN 33

// フェーズ切り替え:
//   PHASE_LISTEN (デフォルト): LISTEN_ONLY でバス上の全フレームを受信
//   コメントアウトすると TWAI_MODE_NORMAL + OBD-II PID スキャンに切り替わる
// #define PHASE_LISTEN

#ifdef PHASE_LISTEN
#define CAN_MODE TWAI_MODE_LISTEN_ONLY
#else
#define CAN_MODE TWAI_MODE_NORMAL
#define RUN_PID_SCAN
#endif

static bool twaiReady = false;

// ----------------------------------------------------------------
// OBD-II ヘルパー（TWAI_MODE_NORMAL 時のみ有効）
// ----------------------------------------------------------------

static bool sendObdRequest(uint8_t pid)
{
  twai_message_t tx = {};
  tx.identifier = 0x18DB33F1; // 29-bit functional addressing (van/truck)
  tx.extd = 1;
  tx.data_length_code = 8;
  tx.data[0] = 0x02; // PCI: Single Frame, length=2
  tx.data[1] = 0x01; // Mode 01
  tx.data[2] = pid;
  // data[3..7] はゼロ初期化済み（ISO 15765-4 パディング）
  return twai_transmit(&tx, pdMS_TO_TICKS(10)) == ESP_OK;
}

// CAN ID=0x7E8 の応答を受信（timeoutMs 内）
static bool receiveObdResponse(uint8_t *data, uint8_t *dlc, uint32_t timeoutMs)
{
  unsigned long deadline = millis() + timeoutMs;
  while (millis() < deadline)
  {
    twai_message_t rx = {};
    if (twai_receive(&rx, pdMS_TO_TICKS(10)) == ESP_OK)
    {
      bool is29bit = rx.extd && (rx.identifier & 0xFFFFFF00) == 0x18DAF100;
      bool is11bit = !rx.extd && rx.identifier == 0x7E8;
      if (is29bit || is11bit)
      {
        memcpy(data, rx.data, 8);
        *dlc = rx.data_length_code;
        return true;
      }
    }
  }
  return false;
}

// PID 0x00/0x20/0x40 を送信し、サポートビットマスク(32bit)を返す
// base=0x00 → PIDs 0x01-0x20, base=0x20 → 0x21-0x40, base=0x40 → 0x41-0x60
static uint32_t getSupportedPidMask(uint8_t supportPid)
{
  twai_message_t txMsg = {};
  esp_err_t txErr;
  txMsg.identifier = 0x18DB33F1; // 29-bit functional addressing
  txMsg.extd = 1;
  txMsg.data_length_code = 8;
  txMsg.data[0] = 0x02;
  txMsg.data[1] = 0x01;
  txMsg.data[2] = supportPid;
  txErr = twai_transmit(&txMsg, pdMS_TO_TICKS(10));
  Serial.printf("[DBG] TX 0x18DB33F1 pid=0x%02X: %s (0x%x)\n",
                supportPid, txErr == ESP_OK ? "OK" : "FAIL", txErr);
  if (txErr != ESP_OK)
    return 0;

  // 応答待ち: 0x7E8 以外のフレームも全部表示する
  unsigned long deadline = millis() + 500;
  while (millis() < deadline)
  {
    twai_message_t rx = {};
    if (twai_receive(&rx, pdMS_TO_TICKS(10)) == ESP_OK)
    {
      Serial.printf("[DBG] RX id=0x%08lX ext=%d len=%d data=", rx.identifier, rx.extd, rx.data_length_code);
      for (int i = 0; i < rx.data_length_code; i++)
        Serial.printf("%02X ", rx.data[i]);
      Serial.println();
      bool is29bit = rx.extd && (rx.identifier & 0xFFFFFF00) == 0x18DAF100;
      bool is11bit = !rx.extd && rx.identifier == 0x7E8;
      if ((is29bit || is11bit) && rx.data[1] == 0x41 && rx.data[2] == supportPid)
      {
        return ((uint32_t)rx.data[3] << 24) | ((uint32_t)rx.data[4] << 16) | ((uint32_t)rx.data[5] << 8) | rx.data[6];
      }
    }
  }
  Serial.printf("[DBG] TX pid=0x%02X: no response\n", supportPid);
  return 0;
}

// MSB = base+1 に対応（例: mask bit31 = PID(base+1)）
static bool isPidSupported(uint32_t mask, uint8_t pid, uint8_t base)
{
  if (pid <= base || pid > base + 32)
    return false;
  uint8_t bit = pid - base - 1;
  return (mask >> (31 - bit)) & 1;
}

// PID をポーリングして応答を返す。サポートされていない場合は false
static bool queryPid(uint8_t pid, uint8_t *data, uint8_t *dlc)
{
  return sendObdRequest(pid) && receiveObdResponse(data, dlc, 300) && data[1] == 0x41 && data[2] == pid;
}

// 各 PID のデコードとシリアル出力
static void printPidResult(uint8_t pid, const char *name,
                           uint32_t mask0, uint32_t mask1, uint32_t mask2,
                           uint32_t mask3 = 0, uint32_t mask4 = 0)
{
  // サポート確認
  bool supported = false;
  if (pid >= 0x01 && pid <= 0x20)
    supported = isPidSupported(mask0, pid, 0x00);
  else if (pid >= 0x21 && pid <= 0x40)
    supported = isPidSupported(mask1, pid, 0x20);
  else if (pid >= 0x41 && pid <= 0x60)
    supported = isPidSupported(mask2, pid, 0x40);
  else if (pid >= 0x61 && pid <= 0x80)
    supported = isPidSupported(mask3, pid, 0x60);
  else if (pid >= 0x81 && pid <= 0xA0)
    supported = isPidSupported(mask4, pid, 0x80);

  if (!supported)
  {
    Serial.printf("[0x%02X] %-20s -- (not supported)\n", pid, name);
    return;
  }

  uint8_t data[8] = {};
  uint8_t dlc = 0;
  if (!queryPid(pid, data, &dlc))
  {
    Serial.printf("[0x%02X] %-20s !! (no response)\n", pid, name);
    return;
  }

  uint8_t A = data[3];
  uint8_t B = data[4];

  switch (pid)
  {
  case 0x04:
    Serial.printf("[0x04] %-20s OK -> %d%%\n", name, (int)(A * 100 / 255));
    break;
  case 0x05:
    Serial.printf("[0x05] %-20s OK -> %d degC\n", name, (int)A - 40);
    break;
  case 0x0A:
    Serial.printf("[0x0A] %-20s OK -> %d kPa\n", name, A * 3);
    break;
  case 0x0B:
    Serial.printf("[0x0B] %-20s OK -> %d kPa (boost: %d kPa)\n",
                  name, A, (int)A - 101);
    break;
  case 0x0C:
    Serial.printf("[0x0C] %-20s OK -> %d rpm\n", name,
                  (int)((A * 256 + B) / 4));
    break;
  case 0x0D:
    Serial.printf("[0x0D] %-20s OK -> %d km/h\n", name, A);
    break;
  case 0x0E:
    Serial.printf("[0x0E] %-20s OK -> %.1f deg BTDC\n",
                  name, A / 2.0f - 64.0f);
    break;
  case 0x0F:
    Serial.printf("[0x0F] %-20s OK -> %d degC\n", name, (int)A - 40);
    break;
  case 0x10:
    Serial.printf("[0x10] %-20s OK -> %.2f g/s\n",
                  name, (A * 256 + B) / 100.0f);
    break;
  case 0x11:
    Serial.printf("[0x11] %-20s OK -> %d%%\n", name, (int)(A * 100 / 255));
    break;
  case 0x2F:
    Serial.printf("[0x2F] %-20s OK -> %d%%\n", name, (int)(A * 100 / 255));
    break;
  case 0x33:
    Serial.printf("[0x33] %-20s OK -> %d kPa\n", name, A);
    break;
  case 0x42:
    Serial.printf("[0x42] %-20s OK -> %.3f V\n", name, (A * 256 + B) / 1000.0f);
    break;
  case 0x5C:
    Serial.printf("[0x5C] %-20s OK -> %d degC\n", name, (int)A - 40);
    break;
  case 0x5E:
    Serial.printf("[0x5E] %-20s OK -> %.2f L/h\n",
                  name, (A * 256 + B) * 0.05f);
    break;
  case 0x61:
  case 0x62:
    Serial.printf("[0x%02X] %-20s OK -> %d%%\n", pid, name, (int)A - 125);
    break;
  case 0x63:
    Serial.printf("[0x63] %-20s OK -> %d Nm\n", name, (int)(A * 256 + B));
    break;
  case 0x66:
  {
    // B*256+C = sensor1 rate in 1/32 g/s units
    uint8_t C = data[5];
    Serial.printf("[0x66] %-20s OK -> sensors=0x%02X MAF1=%.2f g/s\n",
                  name, A, (B * 256 + C) / 32.0f);
    break;
  }
  case 0x67:
    // A=bitmap(bit0=S1,bit1=S2), B=S1 temp, C=S2 temp
    Serial.printf("[0x67] %-20s OK -> sensors=0x%02X S1=%d degC S2=%d degC\n",
                  name, A, (int)B - 40, (int)data[5] - 40);
    break;
  case 0x68:
  {
    // A=bitmap, B=charge air cooler inlet temp, C=outlet temp
    Serial.printf("[0x68] %-20s OK -> sensors=0x%02X in=%d degC out=%d degC\n",
                  name, A, (int)B - 40, (int)data[5] - 40);
    break;
  }
  case 0x70:
  {
    // A=control support bitmap, B*256+C=reference boost, D*256+E=actual boost
    uint8_t D = data[6];
    Serial.printf("[0x70] %-20s OK -> ctrl=0x%02X ref=%d kPa act=%d kPa\n",
                  name, A,
                  (int)(B * 256 + data[5]) - 32767,
                  (int)(D * 256 + data[7]) - 32767);
    break;
  }
  case 0x72:
  {
    // A=sensor bitmap, B*256+C=turbo1 RPM (*2), D*256+E=turbo2 RPM (*2)
    uint8_t C = data[5];
    Serial.printf("[0x72] %-20s OK -> sensors=0x%02X T1=%d rpm\n",
                  name, A, (B * 256 + C) * 2);
    break;
  }
  case 0x7F:
    Serial.printf("[0x7F] %-20s OK -> %lu sec\n",
                  name, ((uint32_t)A << 16) | ((uint32_t)B << 8) | data[5]);
    break;
  default:
    Serial.printf("[0x%02X] %-20s OK -> A=0x%02X B=0x%02X\n",
                  pid, name, A, B);
    break;
  }
}

// ----------------------------------------------------------------
// Arduino エントリポイント
// ----------------------------------------------------------------

static bool twaiInit(twai_mode_t mode)
{
  twai_general_config_t gConfig = TWAI_GENERAL_CONFIG_DEFAULT(
      (gpio_num_t)CAN_TX_PIN, (gpio_num_t)CAN_RX_PIN, mode);
  twai_timing_config_t tConfig = TWAI_TIMING_CONFIG_500KBITS();
  twai_filter_config_t fConfig = TWAI_FILTER_CONFIG_ACCEPT_ALL();
  if (twai_driver_install(&gConfig, &tConfig, &fConfig) != ESP_OK)
    return false;
  if (twai_start() != ESP_OK)
  {
    twai_driver_uninstall();
    return false;
  }
  return true;
}

void setup()
{
  Serial.begin(115200);
  delay(5000);

  pinMode(BTN0_PIN, INPUT_PULLUP);
  pinMode(BTN1_PIN, INPUT_PULLUP);

  // AO3401A パワースイッチ HIGH → MCP2562FD 電源 ON
  pinMode(CAN_EN_PIN, OUTPUT);
  digitalWrite(CAN_EN_PIN, HIGH);
  delay(50);

#ifndef PHASE_LISTEN
  // ---- TX テスト（NO_ACK、オシロ確認用）----
  // PHASE_LISTEN 時はスキップ（実車バスに不要な送信を行わない）
  Serial.println("=== TX test (500kbps, 5sec) ===");
  if (!twaiInit(TWAI_MODE_NO_ACK))
  {
    Serial.println("TWAI init failed (TX phase)");
    return;
  }
  unsigned long txStart = millis();
  uint32_t txCount = 0;
  while (millis() - txStart < 5000)
  {
    twai_message_t tx = {};
    tx.identifier = 0x123;
    tx.data_length_code = 4;
    tx.data[0] = (txCount >> 24) & 0xFF;
    tx.data[1] = (txCount >> 16) & 0xFF;
    tx.data[2] = (txCount >> 8) & 0xFF;
    tx.data[3] = txCount & 0xFF;
    esp_err_t err = twai_transmit(&tx, pdMS_TO_TICKS(10));
    Serial.printf("TX %s cnt=%lu\n", err == ESP_OK ? "ok" : "fail", txCount);
    txCount++;
    delay(2000);
  }
  twai_stop();
  twai_driver_uninstall();
  Serial.println("TX test done. Switching to receive mode...");
  delay(100);
#endif

  // ---- 受信モード（LISTEN_ONLY or NORMAL）----
  if (!twaiInit(CAN_MODE))
  {
    Serial.println("TWAI init failed (RX phase)");
    return;
  }

  twaiReady = true;

#ifdef RUN_PID_SCAN
  Serial.println("=== OBD-II PID Scan (Step B) ===");
  Serial.println("Fetching support bitmasks...");

  uint32_t mask0 = getSupportedPidMask(0x00); // PIDs 0x01-0x20
  delay(1000);
  uint32_t mask1 = getSupportedPidMask(0x20); // PIDs 0x21-0x40
  delay(1000);
  uint32_t mask2 = getSupportedPidMask(0x40); // PIDs 0x41-0x60
  delay(1000);
  uint32_t mask3 = getSupportedPidMask(0x60); // PIDs 0x61-0x80
  delay(1000);
  uint32_t mask4 = getSupportedPidMask(0x80); // PIDs 0x81-0xA0
  delay(1000);
  Serial.printf("Mask[0x00]=0x%08lX  Mask[0x20]=0x%08lX\n", mask0, mask1);
  Serial.printf("Mask[0x40]=0x%08lX  Mask[0x60]=0x%08lX\n", mask2, mask3);
  Serial.printf("Mask[0x80]=0x%08lX\n", mask4);

  Serial.println("--- Priority 1 ---");
  printPidResult(0x04, "Engine Load", mask0, mask1, mask2);
  printPidResult(0x05, "Coolant Temp", mask0, mask1, mask2);
  printPidResult(0x0B, "MAP", mask0, mask1, mask2);
  printPidResult(0x0C, "RPM", mask0, mask1, mask2);
  printPidResult(0x0D, "Speed", mask0, mask1, mask2);
  printPidResult(0x11, "Throttle", mask0, mask1, mask2);

  Serial.println("--- Priority 2 ---");
  printPidResult(0x0A, "Fuel Pressure", mask0, mask1, mask2);
  printPidResult(0x0E, "Ignition Adv", mask0, mask1, mask2);
  printPidResult(0x0F, "Intake Temp", mask0, mask1, mask2);
  printPidResult(0x10, "MAF", mask0, mask1, mask2);
  printPidResult(0x2F, "Fuel Level", mask0, mask1, mask2);
  printPidResult(0x33, "Baro", mask0, mask1, mask2);
  printPidResult(0x42, "ECU Voltage", mask0, mask1, mask2);
  printPidResult(0x5C, "Oil Temp", mask0, mask1, mask2);
  printPidResult(0x5E, "Fuel Rate", mask0, mask1, mask2);

  Serial.println("--- Priority 3 (0x61-0x80) ---");
  printPidResult(0x61, "Torque Demand", mask0, mask1, mask2, mask3);
  printPidResult(0x62, "Torque Actual", mask0, mask1, mask2, mask3);
  printPidResult(0x63, "Torque Ref", mask0, mask1, mask2, mask3);
  printPidResult(0x66, "MAF Alt", mask0, mask1, mask2, mask3);
  printPidResult(0x67, "Coolant Temp Alt", mask0, mask1, mask2, mask3);
  printPidResult(0x68, "Charge Air Temp", mask0, mask1, mask2, mask3);
  printPidResult(0x6C, "Throttle Actuator", mask0, mask1, mask2, mask3);
  printPidResult(0x6E, "Boost Ctrl", mask0, mask1, mask2, mask3);
  printPidResult(0x70, "Boost Pressure", mask0, mask1, mask2, mask3);
  printPidResult(0x72, "Turbo RPM", mask0, mask1, mask2, mask3);
  printPidResult(0x7F, "Engine Run Time", mask0, mask1, mask2, mask3);

  Serial.println("--- Priority 4 (0x81-0xA0) ---");
  printPidResult(0x83, "NOx Sensor", mask0, mask1, mask2, mask3, mask4);
  printPidResult(0x84, "Manifold Surf Temp", mask0, mask1, mask2, mask3, mask4);
  printPidResult(0x87, "MAP Alt", mask0, mask1, mask2, mask3, mask4);
  printPidResult(0x8C, "EGT Bank1", mask0, mask1, mask2, mask3, mask4);
  printPidResult(0x98, "EGT Bank2", mask0, mask1, mask2, mask3, mask4);
  printPidResult(0x9D, "Fuel Inj Timing", mask0, mask1, mask2, mask3, mask4);
  printPidResult(0x9E, "Engine Fuel Rate", mask0, mask1, mask2, mask3, mask4);
  printPidResult(0x9F, "Emission Req", mask0, mask1, mask2, mask3, mask4);

  {
    twai_status_info_t sts;
    twai_get_status_info(&sts);
    Serial.printf("TWAI: state=%d txerr=%lu rxerr=%lu bus_err=%lu arb_lost=%lu\n",
                  (int)sts.state, sts.tx_error_counter, sts.rx_error_counter,
                  sts.bus_error_count, sts.arb_lost_count);
  }
  Serial.println("=== Scan complete. Entering receive-only mode. ===");
#else
  Serial.println("=== Step A: LISTEN_ONLY 500kbps ===");
  Serial.println("Waiting for CAN frames from vehicle...");
  Serial.println("Turn on ignition (IGN ON) and observe frames below.");
#endif
}

#ifdef RUN_PID_SCAN
static void pollObd()
{
  uint8_t data[8];
  uint8_t dlc;

  // --- エンジン基本 ---
  uint16_t rpm     = 0; if (queryPid(0x0C, data, &dlc)) rpm     = (data[3] * 256 + data[4]) / 4;
  uint8_t  spd     = 0; if (queryPid(0x0D, data, &dlc)) spd     = data[3];
  uint8_t  load    = 0; if (queryPid(0x04, data, &dlc)) load    = data[3] * 100 / 255;
  uint8_t  absLoad = 0; if (queryPid(0x43, data, &dlc)) absLoad = (uint32_t)(data[3] * 256 + data[4]) * 100 / 255;
  uint8_t  mapKpa  = 0; if (queryPid(0x0B, data, &dlc)) mapKpa  = data[3];
  uint8_t  tps     = 0; if (queryPid(0x11, data, &dlc)) tps     = data[3] * 100 / 255;
  uint8_t  tpsB    = 0; if (queryPid(0x47, data, &dlc)) tpsB    = data[3] * 100 / 255;
  uint8_t  appD    = 0; if (queryPid(0x49, data, &dlc)) appD    = data[3] * 100 / 255;
  uint8_t  appE    = 0; if (queryPid(0x4A, data, &dlc)) appE    = data[3] * 100 / 255;
  float    ign     = 0; if (queryPid(0x0E, data, &dlc)) ign     = data[3] / 2.0f - 64.0f;
  uint8_t  baro    = 0; if (queryPid(0x33, data, &dlc)) baro    = data[3];
  float    ecuV    = 0; if (queryPid(0x42, data, &dlc)) ecuV    = (data[3] * 256 + data[4]) / 1000.0f;
  uint16_t runTime = 0; if (queryPid(0x1F, data, &dlc)) runTime = data[3] * 256 + data[4];

  // --- 燃料・空燃比 ---
  float   maf     = 0; if (queryPid(0x66, data, &dlc)) maf     = (data[4] * 256 + data[5]) / 32.0f;
  float   stft    = 0; if (queryPid(0x06, data, &dlc)) stft    = (data[3] - 128.0f) * 100.0f / 128.0f;
  float   ltft    = 0; if (queryPid(0x07, data, &dlc)) ltft    = (data[3] - 128.0f) * 100.0f / 128.0f;
  float   lambda  = 0; if (queryPid(0x44, data, &dlc)) lambda  = (data[3] * 256 + data[4]) * 2.0f / 65536.0f;
  uint8_t evap    = 0; if (queryPid(0x2E, data, &dlc)) evap    = data[3] * 100 / 255;

  // --- O2 センサー ---
  // 0x15: A=電圧(A/200 V), B=燃料トリム((B-128)*100/128 %)
  float   o2v     = 0; float o2ft = 0;
  if (queryPid(0x15, data, &dlc)) { o2v = data[3] / 200.0f; o2ft = (data[4] - 128.0f) * 100.0f / 128.0f; }
  // 0x24: A*256+B = lambda×2/65536, C*256+D = 電圧×8/65536 V
  float   wbLambda = 0; float wbV = 0;
  if (queryPid(0x24, data, &dlc)) {
    wbLambda = (data[3] * 256 + data[4]) * 2.0f / 65536.0f;
    wbV      = (data[5] * 256 + data[6]) * 8.0f / 65536.0f;
  }
  float   stft2   = 0; if (queryPid(0x55, data, &dlc)) stft2 = (data[3] - 128.0f) * 100.0f / 128.0f;
  float   ltft2   = 0; if (queryPid(0x56, data, &dlc)) ltft2 = (data[3] - 128.0f) * 100.0f / 128.0f;

  // --- 温度 ---
  int16_t clt     = 0; if (queryPid(0x67, data, &dlc)) clt     = (int16_t)data[4] - 40;
  // 0x3C: (A*256+B)/10 - 40 °C
  float   catTemp = 0; if (queryPid(0x3C, data, &dlc)) catTemp = (data[3] * 256 + data[4]) / 10.0f - 40.0f;

  // --- 走行累積 ---
  uint16_t milDist  = 0; if (queryPid(0x21, data, &dlc)) milDist  = data[3] * 256 + data[4];
  uint8_t  warmups  = 0; if (queryPid(0x30, data, &dlc)) warmups  = data[3];
  uint16_t clrDist  = 0; if (queryPid(0x31, data, &dlc)) clrDist  = data[3] * 256 + data[4];
  uint8_t  fuelType = 0; if (queryPid(0x51, data, &dlc)) fuelType = data[3];

  Serial.println("--- OBD poll ---");
  Serial.printf(" Engine : RPM:%4d SPD:%3dkm/h LOAD:%3d%% ABS:%3d%% MAP:%3dkPa IGN:%+5.1fdeg BARO:%3dkPa\n",
                rpm, spd, load, absLoad, mapKpa, ign, baro);
  Serial.printf(" Throttle: TPS:%3d%% TPSB:%3d%% APP-D:%3d%% APP-E:%3d%%\n",
                tps, tpsB, appD, appE);
  Serial.printf(" Fuel/AFR: MAF:%.2fg/s STFT:%+.1f%% LTFT:%+.1f%% Lambda:%.4f Evap:%3d%%\n",
                maf, stft, ltft, lambda, evap);
  Serial.printf(" O2      : NB=%.3fV(%+.1f%%) WB-L=%.4f WB-V=%.3fV STFT2:%+.1f%% LTFT2:%+.1f%%\n",
                o2v, o2ft, wbLambda, wbV, stft2, ltft2);
  Serial.printf(" Temp    : CLT:%3dC CAT:%.1fC\n", (int)clt, catTemp);
  Serial.printf(" Misc    : ECU:%.3fV RunTime:%us MIL-dist:%ukm Warmups:%u ClrDist:%ukm FuelType:%u\n",
                ecuV, runTime, milDist, warmups, clrDist, fuelType);
}
#endif

void loop()
{
  if (digitalRead(BTN0_PIN) == LOW || digitalRead(BTN1_PIN) == LOW)
  {
    Serial.println("Button pressed, restarting...");
    delay(100);
    ESP.restart();
  }

  if (!twaiReady)
    return;

#ifdef RUN_PID_SCAN
  static unsigned long lastPollMs = 0;
  if (millis() - lastPollMs >= 2000)
  {
    lastPollMs = millis();
    pollObd();
  }
#else
  twai_message_t rx = {};
  if (twai_receive(&rx, pdMS_TO_TICKS(10)) == ESP_OK)
  {
    Serial.printf("RX id=0x%08lX ext=%d len=%d data=", rx.identifier, rx.extd, rx.data_length_code);
    for (int i = 0; i < rx.data_length_code; i++)
      Serial.printf("%02X ", rx.data[i]);
    Serial.println();
  }
#endif

  static unsigned long lastStsMs = 0;
  if (millis() - lastStsMs >= 10000)
  {
    lastStsMs = millis();
    twai_status_info_t sts;
    twai_get_status_info(&sts);
    Serial.printf("[STS] state=%d txerr=%lu rxerr=%lu bus_err=%lu arb_lost=%lu\n",
                  (int)sts.state, sts.tx_error_counter, sts.rx_error_counter,
                  sts.bus_error_count, sts.arb_lost_count);
    if (sts.state == TWAI_STATE_BUS_OFF)
    {
      Serial.println("[STS] BUS_OFF detected, initiating recovery...");
      twai_initiate_recovery();
    }
  }
}
