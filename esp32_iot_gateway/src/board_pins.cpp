#include "board_pins.h"

const BoardPins &boardPinsV1();
const BoardPins &boardPinsV2();

const BoardPins &boardPins()
{
#if BOARD_VERSION == 2
  return boardPinsV2();
#else
  return boardPinsV1();
#endif
}
