#include "can_proxy.h"
#include "../device/can.h"
#include <Arduino.h>
#include <string.h>

namespace
{
// プロトコル定義はcan_proxy.h参照

const uint8_t ACK = '\r';
const uint8_t NACK = '\a'; // BEL (0x07)

// "T"+8桁ID+1桁DLC+16桁DATA=26文字。安全マージンを見て32とする
const size_t CMD_BUF_MAX = 32;

// 未完成コマンド（'\r'が来る前にバイトが途切れた状態）を破棄するまでのアイドル時間。
// PC側の切断・クラッシュ等で中途半端なバイト列が残ると、再接続後の正常なコマンドの
// 先頭に紛れ込んで誤解釈されるため、一定時間バイトが来なければ残骸として捨てる
const uint32_t CMD_IDLE_TIMEOUT_MS = 200;

bool s_working = false;    // 'O'で真、'C'で偽。CAN受信のPCへの転送もこれで制御する
bool s_timestamp = false;  // 'Z1'で真。受信通知の末尾に4桁hexタイムスタンプを付与する

char s_cmdBuf[CMD_BUF_MAX];
size_t s_cmdLen = 0;
uint32_t s_lastByteMs = 0; // s_cmdBufに最後にバイトを追加したtick（アイドルタイムアウト判定用）

char hexChar(uint8_t nibble)
{
  return "0123456789ABCDEF"[nibble & 0x0F];
}

// buf[offset .. offset+count)をhex文字列としてパースする。不正な文字があればfalse
bool parseHex(const char *buf, size_t offset, size_t count, uint32_t &out)
{
  out = 0;
  for (size_t i = 0; i < count; i++)
  {
    char c = buf[offset + i];
    uint8_t nibble;
    if (c >= '0' && c <= '9')
      nibble = (uint8_t)(c - '0');
    else if (c >= 'A' && c <= 'F')
      nibble = (uint8_t)(c - 'A' + 10);
    else if (c >= 'a' && c <= 'f')
      nibble = (uint8_t)(c - 'a' + 10);
    else
      return false;
    out = (out << 4) | nibble;
  }
  return true;
}

void writeHex(uint32_t value, uint8_t digits)
{
  for (int i = digits - 1; i >= 0; i--)
    Serial.write(hexChar((uint8_t)((value >> (i * 4)) & 0x0F)));
}

// 受信したCANフレームをPCへ通知する（'O'済みのときのみ呼ばれる想定）
void notifyFrame(uint32_t id, bool extd, bool rtr, const uint8_t *data, uint8_t dlc)
{
  Serial.write((uint8_t)(extd ? (rtr ? 'R' : 'T') : (rtr ? 'r' : 't')));
  writeHex(id, extd ? 8 : 3);
  Serial.write((uint8_t)hexChar(dlc));
  if (!rtr)
  {
    for (uint8_t i = 0; i < dlc; i++)
    {
      Serial.write((uint8_t)hexChar(data[i] >> 4));
      Serial.write((uint8_t)hexChar(data[i] & 0x0F));
    }
  }
  if (s_timestamp)
    writeHex((uint16_t)(millis() % 60000), 4);
  Serial.write(ACK);
}

// t/T/r/Rコマンドを解析してCAN送信する。フォーマット不正またはCAN未オープンならfalse
bool handleSendFrame(const char *buf, size_t len, bool extd, bool rtr)
{
  size_t idDigits = extd ? 8 : 3;
  size_t pos = 1;
  if (len < pos + idDigits + 1)
    return false;

  uint32_t id;
  if (!parseHex(buf, pos, idDigits, id))
    return false;
  pos += idDigits;

  uint32_t dlc32;
  if (!parseHex(buf, pos, 1, dlc32) || dlc32 > 8)
    return false;
  uint8_t dlc = (uint8_t)dlc32;
  pos += 1;

  uint8_t data[8] = {};
  if (!rtr)
  {
    if (len < pos + (size_t)dlc * 2)
      return false;
    for (uint8_t i = 0; i < dlc; i++)
    {
      uint32_t byteVal;
      if (!parseHex(buf, pos, 2, byteVal))
        return false;
      data[i] = (uint8_t)byteVal;
      pos += 2;
    }
  }

  if (!s_working)
    return false;
  return canRawTransmit(id, extd, data, dlc, rtr);
}

void handleCommand(const char *buf, size_t len)
{
  if (len == 0)
    return;

  switch (buf[0])
  {
  case 'O': // CANを開く（USBシリアルをSLCAN専用にするためquiet=trueでlogger出力を抑止）
    if (canInit(true))
    {
      s_working = true;
      Serial.write(ACK);
    }
    else
    {
      Serial.write(NACK);
    }
    break;

  case 'C': // CANを閉じる（既に閉じていても安全に呼べる）
    canDeinit(true);
    s_working = false;
    Serial.write(ACK);
    break;

  case 't':
    Serial.write(handleSendFrame(buf, len, false, false) ? ACK : NACK);
    break;
  case 'T':
    Serial.write(handleSendFrame(buf, len, true, false) ? ACK : NACK);
    break;
  case 'r':
    Serial.write(handleSendFrame(buf, len, false, true) ? ACK : NACK);
    break;
  case 'R':
    Serial.write(handleSendFrame(buf, len, true, true) ? ACK : NACK);
    break;

  case 'S': // ビットレート確認。このボードは500kbps固定のためS6のみ受け付ける
    Serial.write((len >= 2 && buf[1] == '6') ? ACK : NACK);
    break;

  case 'Z': // 受信通知への4桁hexタイムスタンプ付与 ON/OFF
    if (len >= 2 && (buf[1] == '0' || buf[1] == '1'))
    {
      s_timestamp = (buf[1] == '1');
      Serial.write(ACK);
    }
    else
    {
      Serial.write(NACK);
    }
    break;

  case 'F': // ステータスフラグ照会（エラーなし固定、情報用途のみ）
    Serial.print("F00");
    Serial.write(ACK);
    break;

  case 'V': // バージョン照会（固定値、情報用途のみ）
    Serial.print("V1013");
    Serial.write(ACK);
    break;

  case 'N': // シリアル番号照会（固定値、情報用途のみ）
    Serial.print("N0000");
    Serial.write(ACK);
    break;

  case 'M': // アクセプタンスコード設定（フィルタ未実装、no-op）
  case 'm': // アクセプタンスマスク設定（フィルタ未実装、no-op）
    Serial.write(ACK);
    break;

  default:
    Serial.write(NACK);
    break;
  }
}
} // namespace

void canProxyRun(bool (*shouldExit)())
{
  s_working = false;
  s_timestamp = false;
  s_cmdLen = 0;

  while (!shouldExit())
  {
    // CAN → PC（'O'済みの間だけ、受信した全フレームを通知する）
    if (s_working)
    {
      uint32_t rxId;
      bool rxExtd, rxRtr;
      uint8_t rxData[8];
      uint8_t rxDlc;
      if (canRawReceive(&rxId, &rxExtd, &rxRtr, rxData, &rxDlc, 0))
        notifyFrame(rxId, rxExtd, rxRtr, rxData, rxDlc);
    }

    // 未完成コマンドが一定時間放置されたら残骸として破棄する（切断・クラッシュ対策）
    if (s_cmdLen > 0 && millis() - s_lastByteMs > CMD_IDLE_TIMEOUT_MS)
      s_cmdLen = 0;

    // PC → CAN（'\r'区切りでコマンド行を組み立てて都度処理する）
    while (Serial.available() > 0)
    {
      char c = (char)Serial.read();
      if (c == '\r')
      {
        handleCommand(s_cmdBuf, s_cmdLen);
        s_cmdLen = 0;
      }
      else if (s_cmdLen < CMD_BUF_MAX)
      {
        s_cmdBuf[s_cmdLen++] = c;
        s_lastByteMs = millis();
      }
      else
      {
        // バッファ超過 → 異常なコマンドとみなして破棄
        Serial.write(NACK);
        s_cmdLen = 0;
      }
    }
  }

  if (s_working)
  {
    canDeinit(true); // 'C'を送らずBTN1長押しで抜けた場合の後始末。ここもquiet=trueを維持する
    s_working = false;
  }
}
