// テスト用 S3 URL を事前に定義しておくと URL 入力をスキップできる
// gz_test_setup.ps1 の出力からコピーして設定する。未定義の場合はシリアル入力。
// #define GZ_TEST_PUT_URL "https://s3.ap-northeast-1.amazonaws.com/gz-test-esp32-XXXXX/upload.gz"
// #define GZ_TEST_GET_URL "https://s3.ap-northeast-1.amazonaws.com/gz-test-esp32-XXXXX/test_data.gz"
#define GZ_TEST_PUT_URL "https://s3.ap-northeast-1.amazonaws.com/gz-test-esp32-43364/upload.gz"
#define GZ_TEST_GET_URL "https://s3.ap-northeast-1.amazonaws.com/gz-test-esp32-43364/test_data.gz"

#include "gz_test.h"
#include "https.h"
#include "logger.h"
#include "../device/oled.h"
#include "../device/lte.h"
#include <Arduino.h>
#include <cstring>
#include <cstdio>

extern "C"
{
#include "../../lib/uzlib/uzlib.h"
#include "../../lib/uzlib/defl_static.h"
}

// テストデータ: 0x00〜0xff の 256 バイト固定パターン
static const uint8_t TEST_DATA[256] = {
    0x00,
    0x01,
    0x02,
    0x03,
    0x04,
    0x05,
    0x06,
    0x07,
    0x08,
    0x09,
    0x0a,
    0x0b,
    0x0c,
    0x0d,
    0x0e,
    0x0f,
    0x10,
    0x11,
    0x12,
    0x13,
    0x14,
    0x15,
    0x16,
    0x17,
    0x18,
    0x19,
    0x1a,
    0x1b,
    0x1c,
    0x1d,
    0x1e,
    0x1f,
    0x20,
    0x21,
    0x22,
    0x23,
    0x24,
    0x25,
    0x26,
    0x27,
    0x28,
    0x29,
    0x2a,
    0x2b,
    0x2c,
    0x2d,
    0x2e,
    0x2f,
    0x30,
    0x31,
    0x32,
    0x33,
    0x34,
    0x35,
    0x36,
    0x37,
    0x38,
    0x39,
    0x3a,
    0x3b,
    0x3c,
    0x3d,
    0x3e,
    0x3f,
    0x40,
    0x41,
    0x42,
    0x43,
    0x44,
    0x45,
    0x46,
    0x47,
    0x48,
    0x49,
    0x4a,
    0x4b,
    0x4c,
    0x4d,
    0x4e,
    0x4f,
    0x50,
    0x51,
    0x52,
    0x53,
    0x54,
    0x55,
    0x56,
    0x57,
    0x58,
    0x59,
    0x5a,
    0x5b,
    0x5c,
    0x5d,
    0x5e,
    0x5f,
    0x60,
    0x61,
    0x62,
    0x63,
    0x64,
    0x65,
    0x66,
    0x67,
    0x68,
    0x69,
    0x6a,
    0x6b,
    0x6c,
    0x6d,
    0x6e,
    0x6f,
    0x70,
    0x71,
    0x72,
    0x73,
    0x74,
    0x75,
    0x76,
    0x77,
    0x78,
    0x79,
    0x7a,
    0x7b,
    0x7c,
    0x7d,
    0x7e,
    0x7f,
    0x80,
    0x81,
    0x82,
    0x83,
    0x84,
    0x85,
    0x86,
    0x87,
    0x88,
    0x89,
    0x8a,
    0x8b,
    0x8c,
    0x8d,
    0x8e,
    0x8f,
    0x90,
    0x91,
    0x92,
    0x93,
    0x94,
    0x95,
    0x96,
    0x97,
    0x98,
    0x99,
    0x9a,
    0x9b,
    0x9c,
    0x9d,
    0x9e,
    0x9f,
    0xa0,
    0xa1,
    0xa2,
    0xa3,
    0xa4,
    0xa5,
    0xa6,
    0xa7,
    0xa8,
    0xa9,
    0xaa,
    0xab,
    0xac,
    0xad,
    0xae,
    0xaf,
    0xb0,
    0xb1,
    0xb2,
    0xb3,
    0xb4,
    0xb5,
    0xb6,
    0xb7,
    0xb8,
    0xb9,
    0xba,
    0xbb,
    0xbc,
    0xbd,
    0xbe,
    0xbf,
    0xc0,
    0xc1,
    0xc2,
    0xc3,
    0xc4,
    0xc5,
    0xc6,
    0xc7,
    0xc8,
    0xc9,
    0xca,
    0xcb,
    0xcc,
    0xcd,
    0xce,
    0xcf,
    0xd0,
    0xd1,
    0xd2,
    0xd3,
    0xd4,
    0xd5,
    0xd6,
    0xd7,
    0xd8,
    0xd9,
    0xda,
    0xdb,
    0xdc,
    0xdd,
    0xde,
    0xdf,
    0xe0,
    0xe1,
    0xe2,
    0xe3,
    0xe4,
    0xe5,
    0xe6,
    0xe7,
    0xe8,
    0xe9,
    0xea,
    0xeb,
    0xec,
    0xed,
    0xee,
    0xef,
    0xf0,
    0xf1,
    0xf2,
    0xf3,
    0xf4,
    0xf5,
    0xf6,
    0xf7,
    0xf8,
    0xf9,
    0xfa,
    0xfb,
    0xfc,
    0xfd,
    0xfe,
    0xff,
};

static const uint8_t GZ_HEADER[10] = {
    0x1f,
    0x8b,
    0x08,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0xff,
};

// ─── シリアルから1行読む ────────────────────────────────────────────────────────

static bool readLine(const char *prompt, char *buf, int maxLen)
{
  // 残留データをフラッシュ
  while (Serial.available())
    Serial.read();
  delay(100);

  Serial.println(prompt);
  Serial.setTimeout(60000);
  int n = Serial.readBytesUntil('\n', buf, maxLen - 1);
  while (n > 0 && (buf[n - 1] == '\r' || buf[n - 1] == '\n'))
    n--;
  buf[n] = '\0';
  Serial.printf("  -> %s\n", buf);
  return n > 0;
}

// ─── 圧縮（deflate → gzip ラップ） ────────────────────────────────────────────

// writeDestByte コールバック — NULL のとき出力が捨てられるため必須
static uint8_t s_deflateBuf[512];
static uint32_t s_deflateLen;

static unsigned int deflateWriter(struct uzlib_comp *, unsigned char byte)
{
  if (s_deflateLen < sizeof(s_deflateBuf))
    s_deflateBuf[s_deflateLen++] = byte;
  return 0;
}

static bool compress(uint8_t *outBuf, size_t outBufSize, size_t &outLen)
{
  // ハッシュテーブル (hash_bits=12 → 4096 エントリ × 4 bytes = 16KB)
  static uzlib_hash_entry_t hashTable[1 << 12];
  memset(hashTable, 0, sizeof(hashTable));

  s_deflateLen = 0;

  struct uzlib_comp comp = {};
  comp.writeDestByte = deflateWriter;
  comp.hash_table = hashTable;
  comp.hash_bits = 12;
  comp.grow_buffer = 0;

  // 正しい呼び出し順:
  //   zlib_start_block  → BFINAL=1, BTYPE=01 (static Huffman) ブロックヘッダを書く
  //   uzlib_compress    → データを LZ77+static Huffman で圧縮する
  //   zlib_finish_block → end-of-block コード + ビット列フラッシュ
  zlib_start_block(&comp);
  uzlib_compress(&comp, TEST_DATA, sizeof(TEST_DATA));
  zlib_finish_block(&comp);

  logger.printf("[GZ_TEST] deflate: %u bytes\n", s_deflateLen);

  // CRC32
  uint32_t crc = uzlib_crc32(TEST_DATA, sizeof(TEST_DATA), 0xffffffff) ^ 0xffffffff;
  uint32_t sz = sizeof(TEST_DATA);

  outLen = 10 + s_deflateLen + 8;
  if (outLen > outBufSize)
  {
    logger.printf("[GZ_TEST] output buffer too small (%u > %u)\n", (unsigned)outLen, (unsigned)outBufSize);
    return false;
  }

  uint8_t *p = outBuf;
  memcpy(p, GZ_HEADER, 10);
  p += 10;
  memcpy(p, s_deflateBuf, s_deflateLen);
  p += s_deflateLen;
  p[0] = crc & 0xff;
  p[1] = (crc >> 8) & 0xff;
  p[2] = (crc >> 16) & 0xff;
  p[3] = (crc >> 24) & 0xff;
  p += 4;
  p[0] = sz & 0xff;
  p[1] = (sz >> 8) & 0xff;
  p[2] = (sz >> 16) & 0xff;
  p[3] = (sz >> 24) & 0xff;

  logger.printf("[GZ_TEST] gzip total: %u bytes\n", (unsigned)outLen);

  // デバッグ: CRC32・サイズ（footer）と先頭・末尾バイトを出力して PC の gzip と比較できるようにする
  uint32_t footerCrc = outBuf[outLen - 8] | ((uint32_t)outBuf[outLen - 7] << 8) | ((uint32_t)outBuf[outLen - 6] << 16) | ((uint32_t)outBuf[outLen - 5] << 24);
  uint32_t footerSize = outBuf[outLen - 4] | ((uint32_t)outBuf[outLen - 3] << 8) | ((uint32_t)outBuf[outLen - 2] << 16) | ((uint32_t)outBuf[outLen - 1] << 24);
  logger.printf("[GZ_TEST] footer CRC32=0x%08x size=%u\n", footerCrc, footerSize);

  // deflate 先頭 4 バイト（BFINAL/BTYPE 確認用）
  logger.printf("[GZ_TEST] deflate[0..3]: %02x %02x %02x %02x\n",
                outBuf[10], outBuf[11], outBuf[12], outBuf[13]);

  return true;
}

// ─── 解凍 ──────────────────────────────────────────────────────────────────────

static bool decompress(const uint8_t *gzData, size_t gzLen,
                       uint8_t *outBuf, size_t outBufSize, size_t &outLen)
{
  static uint8_t dict[32768]; // 32KB スライディングウィンドウ

  TINF_DATA d = {};
  d.source = gzData;
  d.source_limit = gzData + gzLen;
  d.dest = outBuf;

  uzlib_uncompress_init(&d, dict, sizeof(dict));

  if (uzlib_gzip_parse_header(&d) != TINF_OK)
  {
    logger.println("[GZ_TEST] gzip header parse failed");
    return false;
  }

  int ret;
  do
  {
    ret = uzlib_uncompress_chksum(&d);
  } while (ret == TINF_OK && (size_t)(d.dest - outBuf) < outBufSize);

  if (ret != TINF_DONE)
  {
    logger.printf("[GZ_TEST] decompress error: %d\n", ret);
    return false;
  }

  outLen = (size_t)(d.dest - outBuf);
  logger.printf("[GZ_TEST] decompressed: %u bytes\n", (unsigned)outLen);
  return true;
}

// ─── エントリポイント ──────────────────────────────────────────────────────────

void runGzTest()
{
  static char putUrl[512];
  static char getUrl[512];
  static uint8_t gzBuf[600];
  static uint8_t getRecvBuf[600];
  static uint8_t decompBuf[512];

  oledShowMessage("GZ Test", "Connect Serial");
  Serial.println("\n=== GZ COMPRESS/DECOMPRESS TEST ===");

#ifdef GZ_TEST_PUT_URL
  strncpy(putUrl, GZ_TEST_PUT_URL, sizeof(putUrl) - 1);
  putUrl[sizeof(putUrl) - 1] = '\0';
  Serial.printf("PUT URL: (defined) %s\n", putUrl);
#else
  if (!readLine("PUT URL:", putUrl, sizeof(putUrl)))
  {
    Serial.println("Aborted (no URL entered).");
    oledShowMessage("GZ Test", "Aborted");
    return;
  }
#endif

#ifdef GZ_TEST_GET_URL
  strncpy(getUrl, GZ_TEST_GET_URL, sizeof(getUrl) - 1);
  getUrl[sizeof(getUrl) - 1] = '\0';
  Serial.printf("GET URL: (defined) %s\n", getUrl);
#else
  if (!readLine("GET URL:", getUrl, sizeof(getUrl)))
  {
    Serial.println("Aborted (no URL entered).");
    oledShowMessage("GZ Test", "Aborted");
    return;
  }
#endif

  // ── Step 1: 圧縮 ────────────────────────────────────────────────────────────

  Serial.println("\n[1/4] Compressing TEST_DATA (256 bytes)...");
  oledShowMessage("GZ Test", "1/4 Compressing");

  size_t gzLen = 0;
  if (!compress(gzBuf, sizeof(gzBuf), gzLen))
  {
    Serial.println("FAIL: compress error");
    oledShowMessage("GZ Test", "FAIL: compress");
    return;
  }
  Serial.printf("  OK: %u bytes\n", (unsigned)gzLen);

  // ── Step 2: PUT ─────────────────────────────────────────────────────────────

  Serial.println("[2/4] PUT to S3...");
  oledShowMessage("GZ Test", "2/4 PUT");

  // AT+SHCONN は MQTT 接続中に失敗する場合がある。事前に MQTT を切断する
  // lte.sendCmd("AT+SMDISC");
  delay(500);

  int putCode = https.put(putUrl, gzBuf, gzLen);
  Serial.printf("  HTTP %d\n", putCode);
  if (putCode != 200)
  {
    Serial.printf("FAIL: PUT returned %d\n", putCode);
    oledShowMessage("GZ Test", "FAIL: PUT");
    return;
  }
  Serial.println("  OK");

  // ── Step 3: GET ─────────────────────────────────────────────────────────────

  Serial.println("[3/4] GET from S3...");
  oledShowMessage("GZ Test", "3/4 GET");

  size_t recvLen = 0;
  int getCode = https.get(getUrl, [&](const uint8_t *chunk, size_t len) -> bool
                          {
    if (recvLen + len > sizeof(getRecvBuf)) {
      logger.println("[GZ_TEST] recv buffer overflow");
      return false;
    }
    memcpy(getRecvBuf + recvLen, chunk, len);
    recvLen += len;
    return true; });

  Serial.printf("  HTTP %d, %u bytes received\n", getCode, (unsigned)recvLen);
  if (getCode != 200 || recvLen == 0)
  {
    Serial.printf("FAIL: GET returned %d\n", getCode);
    oledShowMessage("GZ Test", "FAIL: GET");
    return;
  }

  // ── Step 4: 解凍して検証 ────────────────────────────────────────────────────

  Serial.println("[4/4] Decompress and verify...");
  oledShowMessage("GZ Test", "4/4 Decompress");

  size_t decompLen = 0;
  if (!decompress(getRecvBuf, recvLen, decompBuf, sizeof(decompBuf), decompLen))
  {
    Serial.println("FAIL: decompress error");
    oledShowMessage("GZ Test", "FAIL: decomp");
    return;
  }

  if (decompLen != sizeof(TEST_DATA))
  {
    Serial.printf("FAIL: size mismatch (%u != %u)\n", (unsigned)decompLen, (unsigned)sizeof(TEST_DATA));
    oledShowMessage("GZ Test", "FAIL: size");
    return;
  }

  if (memcmp(decompBuf, TEST_DATA, sizeof(TEST_DATA)) != 0)
  {
    // 不一致バイトを探して報告
    for (size_t i = 0; i < sizeof(TEST_DATA); i++)
    {
      if (decompBuf[i] != TEST_DATA[i])
      {
        Serial.printf("FAIL: byte[%u] expected 0x%02x got 0x%02x\n",
                      (unsigned)i, TEST_DATA[i], decompBuf[i]);
        break;
      }
    }
    oledShowMessage("GZ Test", "FAIL: mismatch");
    return;
  }

  Serial.println("\n=== PASS: all 256 bytes match ===");
  oledShowMessage("GZ Test", "PASS!");
  delay(3000);
}
