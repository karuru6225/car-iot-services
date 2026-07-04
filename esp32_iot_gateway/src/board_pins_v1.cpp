#include "board_pins.h"

// m5atom_power_adc v1（現行基板）のピン配置
const BoardPins &boardPinsV1()
{
  static const BoardPins pins = {
      /* lteRxPin  */ 7,
      /* lteTxPin  */ 8,
      /* lteEnPin  */ 9,
      /* btn0Pin   */ 26,
      /* btn1Pin   */ 33,
      /* buzzerPin */ 34,
      /* sdaPin    */ 17,
      /* sclPin    */ 18,
      /* chgOnPin  */ 21,
      /* relay0Pin */ 13,
      /* relay1Pin */ 14,
      /* relay2Pin */ 15,
      /* gu0Pin    */ 4,
      /* gu1Pin    */ 5,
      /* guEnPin   */ 6,
  };
  return pins;
}
