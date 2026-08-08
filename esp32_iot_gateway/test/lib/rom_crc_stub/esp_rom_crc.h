// nativeテスト専用スタブ。実機はESP-IDFのROM関数(esp_rom_crc.h)を使うが、
// nativeプラットフォームには存在しないためtelemetry.cppのリンクを通すためだけに用意する。
// CRC値そのものの正しさ（実機ROM実装とのビット一致）はテストで検証していない。
// MsgPackTelemetryEncoder（このCRCを実際に使う唯一の箇所）はnativeテスト対象外にしている。
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint32_t esp_rom_crc32_le(uint32_t crc, const uint8_t *buf, uint32_t len);

#ifdef __cplusplus
}
#endif
