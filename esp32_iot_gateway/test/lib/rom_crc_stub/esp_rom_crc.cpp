#include "esp_rom_crc.h"

// 標準CRC-32（反転、poly 0xEDB88320、init/xorout 0xFFFFFFFF）のビット単位実装。
// 実機ROM実装とのビット完全一致は未検証のスタブ。
uint32_t esp_rom_crc32_le(uint32_t crc, const uint8_t *buf, uint32_t len)
{
  crc = ~crc;
  for (uint32_t i = 0; i < len; i++)
  {
    crc ^= buf[i];
    for (int bit = 0; bit < 8; bit++)
      crc = (crc >> 1) ^ (0xEDB88320u & (~(crc & 1u) + 1u));
  }
  return ~crc;
}
