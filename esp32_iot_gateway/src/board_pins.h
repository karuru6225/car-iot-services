#pragma once
#include <stdint.h>

// m5atom_power_adc 基板バージョン。platformio.ini の build_flags で指定
#ifndef BOARD_VERSION
#define BOARD_VERSION 1
#endif

// 基板の未搭載ピンを表す番兵値
static constexpr uint8_t PIN_UNUSED = 0xFF;

struct BoardPins
{
  uint8_t lteRxPin;
  uint8_t lteTxPin;
  uint8_t lteEnPin;
  uint8_t btn0Pin;
  uint8_t btn1Pin;
  uint8_t buzzerPin;
  uint8_t sdaPin;
  uint8_t sclPin;
  uint8_t chgOnPin;
  uint8_t relay0Pin;
  uint8_t relay1Pin;
  uint8_t relay2Pin;
  uint8_t gu0Pin;
  uint8_t gu1Pin;
  uint8_t guEnPin;
  uint8_t pwrHoldPin; // v2のみ。自己保持回路の電源保持ピン（PIN_UNUSED = 非搭載）
  uint8_t gp2Pin;     // v2のみ。J108 I2C拡張コネクタ（PIN_UNUSED = 非搭載）
  uint8_t gp3Pin;     // v2のみ。J108 I2C拡張コネクタ（PIN_UNUSED = 非搭載）
  uint8_t gp11Pin;    // v2のみ。J107 I2C拡張コネクタ（PIN_UNUSED = 非搭載）
  uint8_t gp12Pin;    // v2のみ。J107 I2C拡張コネクタ（PIN_UNUSED = 非搭載）
};

// 現在ビルド対象の基板バージョンに対応するピン配置を返す
const BoardPins &boardPins();
