package info.karuru.cariot.obd

import org.junit.Assert.assertEquals
import org.junit.Test

// テストリスト（esp32_iot_gateway/src/domain/obd.cpp の obdCrc8() と同一アルゴリズム、
// 多項式0x07・初期値0x00であることをmobile/lib/models/obd_reading.dartのDart実装と
// 突き合わせたテストベクタで確認する）
// - [x] 空バイト列 -> 0x00
// - [x] [0x00] -> 0x00
// - [x] [0x01] -> 0x07
// - [x] [0xFF] -> 0xF3
// - [x] "123456789" -> 0xF4
// - [x] [0x01,0x02,0x03,0x04,0x05] -> 0xBC
// - [x] 0..19の連番20バイト -> 0x27
// - [x] lenを指定すると、bytes全体ではなく先頭lenバイトだけで計算する
class Crc8Test {
  @Test
  fun `空バイト列のCRCは0x00`() {
    assertEquals(0x00, crc8(byteArrayOf()))
  }

  @Test
  fun `0x00 1バイトのCRCは0x00`() {
    assertEquals(0x00, crc8(byteArrayOf(0x00)))
  }

  @Test
  fun `0x01 1バイトのCRCは0x07`() {
    assertEquals(0x07, crc8(byteArrayOf(0x01)))
  }

  @Test
  fun `0xFF 1バイトのCRCは0xF3`() {
    assertEquals(0xF3, crc8(byteArrayOf(0xFF.toByte())))
  }

  @Test
  fun `文字列123456789のCRCは0xF4`() {
    assertEquals(0xF4, crc8("123456789".toByteArray(Charsets.US_ASCII)))
  }

  @Test
  fun `連番5バイトのCRCは0xBC`() {
    assertEquals(0xBC, crc8(byteArrayOf(0x01, 0x02, 0x03, 0x04, 0x05)))
  }

  @Test
  fun `0から19の連番20バイトのCRCは0x27`() {
    val bytes = ByteArray(20) { it.toByte() }
    assertEquals(0x27, crc8(bytes))
  }

  @Test
  fun `lenを指定すると先頭lenバイトだけで計算する`() {
    // 末尾に余計なバイトが付いていても、len=1なら[0x01]だけで計算した0x07になる
    assertEquals(0x07, crc8(byteArrayOf(0x01, 0x02, 0x03), len = 1))
  }
}
