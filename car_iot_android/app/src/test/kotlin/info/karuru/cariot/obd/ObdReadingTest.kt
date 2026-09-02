package info.karuru.cariot.obd

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

// テストリスト（mobile/lib/models/obd_reading.dartの移植。ObdBlePacket
// esp32_iot_gateway/src/domain/obd.h とフィールド順・型を完全一致させる）
// - [x] 空バイト列はnullを返す
// - [x] CRC不一致はnullを返す（パース自体を拒否）
// - [x] 全フィールドゼロの正常パケットはvalid=trueでパースされる
// - [x] u8/i8/u16/i16/u32/f32の各型が正しくリトルエンディアンで読める（代表値・境界値）
// - [x] schemaVersion不一致でも読み取りは続行される（警告のみ、パース拒否しない）
// - [x] ボディ読み取り位置がextOffsetを超えたらvalid=falseになり、以降のフィールドはデフォルト値のまま
// - [x] ボディ読み取り完了後の位置がextOffsetと一致しなければvalid=falseになる
// - [x] TLV拡張フィールド(atfTempC/atfTempValid)が正しくパースされる
// - [x] 未知のTLV fieldIdはlen分読み飛ばして後続のフィールドは正しく読める
// - [x] TLV拡張データが壊れていて範囲外読み取りになる場合もクラッシュせずvalid判定はボディの
//       パース結果のまま返す（TLV拡張の異常でボディの読み取り結果まで無効にはしない）
class ObdReadingTest {
  @Test
  fun `空バイト列はnullを返す`() {
    assertNull(ObdReading.fromBytes(byteArrayOf()))
  }

  @Test
  fun `CRC不一致はnullを返す`() {
    val bytes = zeroBodyPacket().build()
    bytes[bytes.size - 1] = (bytes[bytes.size - 1] + 1).toByte() // CRCバイトを壊す
    assertNull(ObdReading.fromBytes(bytes))
  }

  @Test
  fun `全フィールドゼロの正常パケットはvalidがtrueでパースされる`() {
    val reading = ObdReading.fromBytes(zeroBodyPacket().build())!!
    assertTrue(reading.valid)
    assertEquals(0, reading.rpm)
    assertEquals(0, reading.iat2C)
  }

  @Test
  fun `u16フィールドはリトルエンディアンで正しく読める`() {
    val reading = ObdReading.fromBytes(zeroBodyPacket().apply { rpm = 0x1234 }.build())!!
    assertEquals(0x1234, reading.rpm)
  }

  @Test
  fun `i8フィールドは符号付きで正しく読める`() {
    val reading = ObdReading.fromBytes(zeroBodyPacket().apply { boostKpa = -50 }.build())!!
    assertEquals(-50, reading.boostKpa)
  }

  @Test
  fun `i16フィールドは符号付きで正しく読める`() {
    val reading = ObdReading.fromBytes(zeroBodyPacket().apply { coolantC = -20 }.build())!!
    assertEquals(-20, reading.coolantC)
  }

  @Test
  fun `u32フィールドが正しく読める`() {
    val reading = ObdReading.fromBytes(zeroBodyPacket().apply { ts = 0x12345678L }.build())!!
    assertEquals(0x12345678L, reading.ts)
  }

  @Test
  fun `f32フィールドが正しく読める`() {
    val reading = ObdReading.fromBytes(zeroBodyPacket().apply { ecuVoltage = 13.75f }.build())!!
    assertEquals(13.75f, reading.ecuVoltage, 0.0001f)
  }

  @Test
  fun `schemaVersion不一致でも読み取りは続行される`() {
    val reading = ObdReading.fromBytes(
        zeroBodyPacket().apply { schemaVersion = 99; rpm = 0x1234 }.build()
    )!!
    assertEquals(0x1234, reading.rpm)
  }

  @Test
  fun `ボディが短すぎてextOffsetを超えて読もうとするとvalidがfalseになる`() {
    // headerLenは正しいがボディが1バイトしかない不正なパケットを直接組み立てる
    // （PacketBuilderは常に正しい35フィールド分のボディを書くため、ここだけは手組みする）
    val core = byteArrayOf(
        1, // schemaVersion
        8, // headerLen
        9, // extOffset = headerLen + 1（ボディ1バイトしかない）
        1, // valid
        0, 0, 0, 0, // validMask
        0x00, // ボディ: 1バイトだけ（rpmのu16すら読み切れない）
    )
    val crc = crc8(core)
    val reading = ObdReading.fromBytes(core + crc.toByte())!!
    assertFalse(reading.valid)
  }

  @Test
  fun `ボディ読み取り完了後の位置がextOffsetと一致しなければvalidがfalseになる`() {
    // ボディ末尾にダミーパディング1バイト(0x00)を足してbytes全体のサイズを確保した上で、
    // ヘッダのextOffset申告値だけを実際のボディ終端より1バイト大きくずらす。limitが
    // ボディ終端より後ろになるためオーバーランは起きないが、読み取り完了後の位置
    // (実際のボディ終端)がextOffsetの申告値と一致しない状態になり、r.pos != extOffsetの
    // 分岐でvalid=falseになる。パディングは0x00なのでextCount=0として解釈され、
    // TLV拡張パースには影響しない（bytes.size == extOffsetでTLVブロック自体に入らない）。
    val bytes = (zeroBodyPacket().build().dropLast(1) + listOf(0x00.toByte())).toMutableList()
    bytes[2] = (bytes[2] + 1).toByte() // extOffset(index=2)を+1
    val crc = crc8(bytes.toByteArray())
    val reading = ObdReading.fromBytes((bytes + crc.toByte()).toByteArray())!!
    assertFalse(reading.valid)
  }

  @Test
  fun `TLV拡張フィールドatfTempCとatfTempValidが正しくパースされる`() {
    val bytes = zeroBodyPacket()
        .extField(1, byteArrayOf(0x64, 0x00)) // atfTempC = 0x0064 = 100 (i16 LE)
        .extField(2, byteArrayOf(0x01)) // atfTempValid = true
        .build()
    val reading = ObdReading.fromBytes(bytes)!!
    assertEquals(100, reading.atfTempC)
    assertTrue(reading.atfTempValid)
  }

  @Test
  fun `未知のTLV fieldIdはlen分読み飛ばして後続フィールドは正しく読める`() {
    val bytes = zeroBodyPacket()
        .extField(99, byteArrayOf(0x01, 0x02, 0x03)) // 未知のfieldId、3バイト
        .extField(2, byteArrayOf(0x01)) // atfTempValid = true（未知フィールドの後）
        .build()
    val reading = ObdReading.fromBytes(bytes)!!
    assertTrue(reading.atfTempValid)
  }

  @Test
  fun `TLV拡張データが壊れていて範囲外読み取りになってもクラッシュせずボディのvalid判定を返す`() {
    // fieldId=1(atfTempC, i16=2バイト必要)だがlen=1しか無い壊れたTLVを組み立てる。
    // 正直にlen分読み飛ばさずatfTempCとして2バイト読もうとすると範囲外アクセスになりうる
    // 状況を再現する（bytes末尾ぎりぎりにfieldId=1を配置）。
    val bytes = zeroBodyPacket()
        .extField(1, byteArrayOf(0x01)) // atfTempCのつもりだがlen=1しかない壊れたデータ
        .build()
    val reading = ObdReading.fromBytes(bytes)!!
    assertTrue(reading.valid) // ボディ自体は正常なのでvalidはtrueのまま
  }
}
