#include "board_pins.h"

// m5atom_power_adc v2（自己保持回路・電源SW追加基板）のピン配置
const BoardPins &boardPinsV2()
{
  static const BoardPins pins = {
      /* gu00Pin    */ 7,
      /* gu01Pin    */ 8,
      /* gu0EnPin   */ 9,
      /* btn0Pin    */ 26,
      /* btn1Pin    */ 33,
      /* buzzerPin  */ 34,
      /* sdaPin     */ 17,
      /* sclPin     */ 18,
      /* chgOnPin   */ 21,
      /* relay0Pin  */ 13,
      /* relay1Pin  */ 14,
      /* relay2Pin  */ 15,
      /* gu10Pin    */ 4,
      /* gu11Pin    */ 5,
      /* gu1EnPin   */ 6,
      /* pwrHoldPin */ 10,
      /* gp2Pin     */ 2,
      /* gp3Pin     */ 3,
      /* gp11Pin    */ 11,
      /* gp12Pin    */ 12,
  };
  return pins;
}
