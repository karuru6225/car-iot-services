#include "board_pins.h"

// m5atom_power_adc v1（現行基板）のピン配置
const BoardPins &boardPinsV1()
{
  static const BoardPins pins = {
      /* gu00Pin   */ 4,
      /* gu01Pin   */ 5,
      /* gu0EnPin  */ 6,
      /* btn0Pin   */ 26,
      /* btn1Pin   */ 33,
      /* buzzerPin */ 34,
      /* sdaPin    */ 17,
      /* sclPin    */ 18,
      /* chgOnPin  */ 21,
      /* relay0Pin */ 11,
      /* relay1Pin */ 13,
      /* relay2Pin */ 15,
      /* gu10Pin   */ 7,
      /* gu11Pin   */ 8,
      /* gu1EnPin  */ 9,
      /* pwrHoldPin */ PIN_UNUSED,
      /* gp2Pin     */ PIN_UNUSED,
      /* gp3Pin     */ PIN_UNUSED,
      /* gp11Pin    */ PIN_UNUSED,
      /* gp12Pin    */ PIN_UNUSED,
  };
  return pins;
}
