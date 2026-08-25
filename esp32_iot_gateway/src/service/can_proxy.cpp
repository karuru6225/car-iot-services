#include "can_proxy.h"
#include "../device/can.h"
#include <Arduino.h>
#include <string.h>

namespace
{
// プロトコル定義はcan_proxy.h参照
const uint8_t SYNC_TO_CAN = 0x55;   // PC→ESP32（このバイトから始まるフレームをCANへ送信する）
const uint8_t SYNC_FROM_CAN = 0xAA; // ESP32→PC（CAN受信フレームの転送に使う）

// PC→ESP32のバイト列を1バイトずつ受け取って組み立てる状態機械。
// Serial.available()の到着タイミングはバイト単位でバラバラなため、呼び出しをまたいで状態を保持する。
enum class ParseState
{
  WaitSync, // 0x55を待つ（それ以外のバイトは読み捨てる）
  Flags,
  Id,
  Dlc,
  Data,
  Checksum, // FLAGS+ID+DLC+DATA全体のXOR（0x55/0xAAとの偶然の一致対策、can_proxy.h参照）
};

ParseState s_state = ParseState::WaitSync;
uint8_t s_flags = 0;
uint32_t s_id = 0;
uint8_t s_idBytesRead = 0;
uint8_t s_dlc = 0;
uint8_t s_data[8];
uint8_t s_dataBytesRead = 0;
uint8_t s_checksum = 0; // 受信中に随時更新する累積XOR

// 1バイト受信するごとに呼ぶ。フレームが完成し、かつチェックサムが一致すればtrueを返し
// out引数に結果を書く。チェックサム不一致の場合はfalseのままフレームを破棄する
// （偶然0x55にマッチしたゴミデータをCAN送信してしまわないための安全弁）
bool feedByte(uint8_t b, uint32_t &idOut, bool &extdOut, uint8_t *dataOut, uint8_t &dlcOut)
{
  switch (s_state)
  {
  case ParseState::WaitSync:
    if (b == SYNC_TO_CAN)
    {
      s_state = ParseState::Flags;
      s_checksum = 0;
    }
    return false;

  case ParseState::Flags:
    s_flags = b;
    s_checksum ^= b;
    s_id = 0;
    s_idBytesRead = 0;
    s_state = ParseState::Id;
    return false;

  case ParseState::Id:
    s_id |= ((uint32_t)b) << (8 * s_idBytesRead);
    s_checksum ^= b;
    s_idBytesRead++;
    if (s_idBytesRead >= 4)
      s_state = ParseState::Dlc;
    return false;

  case ParseState::Dlc:
    if (b > 8)
    {
      // 不正なDLC → このフレームは破棄し、同期待ちへ戻る
      s_state = ParseState::WaitSync;
      return false;
    }
    s_dlc = b;
    s_checksum ^= b;
    s_dataBytesRead = 0;
    s_state = (s_dlc == 0) ? ParseState::Checksum : ParseState::Data;
    return false;

  case ParseState::Data:
    s_data[s_dataBytesRead++] = b;
    s_checksum ^= b;
    if (s_dataBytesRead >= s_dlc)
      s_state = ParseState::Checksum;
    return false;

  case ParseState::Checksum:
    s_state = ParseState::WaitSync; // 一致・不一致に関わらず次フレームの同期待ちへ
    if (b != s_checksum)
      return false; // 不一致 → 偶然の一致とみなして破棄（CANへは送信しない）
    idOut = s_id;
    extdOut = (s_flags & 0x01) != 0;
    dlcOut = s_dlc;
    memcpy(dataOut, s_data, s_dlc);
    return true;
  }
  return false; // 到達しない（全ケース網羅済み）
}

void writeOutgoing(uint32_t id, bool extd, const uint8_t *data, uint8_t dlc)
{
  uint8_t flags = extd ? (uint8_t)0x01 : (uint8_t)0x00;
  uint8_t idBytes[4] = {
      (uint8_t)(id & 0xFF), (uint8_t)((id >> 8) & 0xFF),
      (uint8_t)((id >> 16) & 0xFF), (uint8_t)((id >> 24) & 0xFF)};

  uint8_t checksum = flags ^ dlc;
  for (uint8_t i = 0; i < 4; i++)
    checksum ^= idBytes[i];
  for (uint8_t i = 0; i < dlc; i++)
    checksum ^= data[i];

  Serial.write(SYNC_FROM_CAN);
  Serial.write(flags);
  Serial.write(idBytes, sizeof(idBytes));
  Serial.write(dlc);
  if (dlc > 0)
    Serial.write(data, dlc);
  Serial.write(checksum);
}
} // namespace

void canProxyRun(bool (*shouldExit)())
{
  s_state = ParseState::WaitSync; // 前回の呼び出しの残骸を引き継がないようリセット

  while (!shouldExit())
  {
    // CAN → PC（フィルタなし、受信した全フレームを転送する）
    uint32_t rxId;
    bool rxExtd;
    uint8_t rxData[8];
    uint8_t rxDlc;
    if (canRawReceive(&rxId, &rxExtd, rxData, &rxDlc, 0))
      writeOutgoing(rxId, rxExtd, rxData, rxDlc);

    // PC → CAN（届いているバイトを溜めずに随時消化する）
    while (Serial.available() > 0)
    {
      uint32_t txId;
      bool txExtd;
      uint8_t txData[8];
      uint8_t txDlc;
      if (feedByte((uint8_t)Serial.read(), txId, txExtd, txData, txDlc))
        canRawTransmit(txId, txExtd, txData, txDlc);
    }
  }
}
