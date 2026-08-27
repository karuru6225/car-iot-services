package info.karuru.cariot.obd

import java.nio.ByteBuffer
import java.nio.ByteOrder

// _Readerの読み取りがlimitを超えた際にthrowされる。境界がズレている以上、それ以前に読んだ
// ボディの値も正しいオフセットで読めている保証がないため、以降の読み取りを続ける意味がない。
// ObdReading.fromBytes()がこれをキャッチしてvalid=falseにし、残りのフィールド読み取りを打ち切る。
private class ReaderOverrunException : Exception()

// バイト列を先頭から順に読み進めるカーソル。読み取りメソッドを呼ぶたびに内部位置を
// 自動で進めるため、呼び出し側（ObdReading.fromBytes()）はオフセットを一切書かずに済む。
// ObdBlePacket（esp32_iot_gateway/src/domain/obd.h）のフィールド宣言順と呼び出し順を
// 一致させることだけが要件になる。
private class Reader(bytes: ByteArray, start: Int) {
  private val buf = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN)
  private var limit: Int? = null
  var pos: Int = start
    private set

  fun seekTo(newPos: Int) {
    pos = newPos
  }

  fun setLimit(l: Int) {
    limit = l
  }

  private fun checkLimit() {
    val l = limit
    if (l != null && pos > l) {
      throw ReaderOverrunException()
    }
  }

  // バイト列の実サイズを超えて読もうとした場合も、setLimit()で指定した境界を超えた場合と
  // 同じReaderOverrunExceptionとして扱う（ByteBufferの範囲外アクセス例外をそのまま
  // 伝播させず、呼び出し側は一種類の例外だけをキャッチすればよいようにする）。
  private fun ensureAvailable(size: Int) {
    if (pos + size > buf.capacity()) {
      throw ReaderOverrunException()
    }
  }

  fun u8(): Int {
    ensureAvailable(1)
    val v = buf.get(pos).toInt() and 0xFF
    pos += 1
    checkLimit()
    return v
  }

  fun i8(): Int {
    ensureAvailable(1)
    val v = buf.get(pos).toInt()
    pos += 1
    checkLimit()
    return v
  }

  fun u16(): Int {
    ensureAvailable(2)
    val v = buf.getShort(pos).toInt() and 0xFFFF
    pos += 2
    checkLimit()
    return v
  }

  fun i16(): Int {
    ensureAvailable(2)
    val v = buf.getShort(pos).toInt()
    pos += 2
    checkLimit()
    return v
  }

  fun u32(): Long {
    ensureAvailable(4)
    val v = buf.getInt(pos).toLong() and 0xFFFFFFFFL
    pos += 4
    checkLimit()
    return v
  }

  fun f32(): Float {
    ensureAvailable(4)
    val v = buf.getFloat(pos)
    pos += 4
    checkLimit()
    return v
  }
}

// ObdReading（esp32_iot_gateway/src/domain/obd.h の ObdBlePacket）のKotlin側パース結果。
// コア構造体（bytes[0..extOffset)）はヘッダ（schemaVersion/headerLen/extOffset/valid/
// validMask）とボディ（実データ）で構成される。フィールド順・オフセットは ObdBlePacket
// と完全一致させること。extOffset以降にはTLV拡張フィールド領域が続く（ObdExtFieldId参照）。
data class ObdReading(
    val rpm: Int,
    val speedKmh: Int,
    val loadPct: Int,
    val mapKpa: Int,
    val baroKpa: Int,
    val boostKpa: Int,
    val throttlePct: Int,
    val timingDeg: Float,
    val ecuVoltage: Float,
    val mafGs: Float,
    val coolantC: Int,
    val fuelRateLph: Float,
    val stftPct: Float,
    val ltftPct: Float,
    val o2B1s2V: Float,
    val o2B1s2TrimPct: Float,
    val engineRunTimeSec: Int,
    val milDistanceKm: Int,
    val o2S1Ratio: Float,
    val o2S1Voltage: Float,
    val evapPurgePct: Int,
    val warmupsSinceCleared: Int,
    val distanceSinceClearedKm: Int,
    val catalystTempC: Float,
    val absoluteLoadPct: Float,
    val commandedAfr: Float,
    val throttleBPct: Int,
    val accelPedalDPct: Int,
    val accelPedalEPct: Int,
    val fuelType: Int,
    val secO2TrimStPct: Float,
    val secO2TrimLtPct: Float,
    val valid: Boolean,
    val ts: Long,
    val iatC: Int,
    val iat2C: Int,
    val validMask: Long,
    // 以下はTLV拡張フィールド領域から読み取る（コア構造体には含まれない）
    val atfTempC: Int,
    val atfTempValid: Boolean,
) {
  companion object {
    private const val EXT_FIELD_ATF_TEMP_C = 1
    private const val EXT_FIELD_ATF_TEMP_VALID = 2

    // ファーム側 domain/obd.h の OBD_BLE_SCHEMA_VERSION と対応させること。
    private const val SCHEMA_VERSION = 1

    // パース失敗時（バイト数不足）はnullを返す。
    fun fromBytes(rawBytes: ByteArray): ObdReading? {
      // 末尾1バイトはCRC-8（伝送中のビット化け検出用）。CRC不一致は物理的な通信異常のため、
      // 以降の処理は一切行わず即座にパースを拒否する。
      if (rawBytes.isEmpty()) return null
      val dataLen = rawBytes.size - 1
      val receivedCrc = rawBytes[dataLen].toInt() and 0xFF
      val computedCrc = crc8(rawBytes, dataLen)
      if (receivedCrc != computedCrc) return null

      val bytes = rawBytes.copyOfRange(0, dataLen)
      if (bytes.size < 2) return null

      val r = Reader(bytes, 0)
      // schemaVersion不一致自体はパース拒否せず、読み取りは続行する
      // （headerLen/extOffsetで拡張領域の位置は自己記述化されているため）。
      r.u8() // schemaVersion（値自体は使わない、読み進めるためだけ）

      val headerLen = r.u8()
      if (bytes.size < headerLen) return null

      val extOffset = r.u8()
      if (bytes.size < extOffset) return null

      var valid = r.u8() != 0
      val validMask = r.u32()

      // headerLenへ明示的にシークしてからボディを読む（未知のヘッダフィールドが将来
      // 追加されても安全にボディへたどり着ける）。
      r.seekTo(headerLen)
      r.setLimit(extOffset)

      var rpm = 0
      var speedKmh = 0
      var loadPct = 0
      var mapKpa = 0
      var baroKpa = 0
      var boostKpa = 0
      var throttlePct = 0
      var timingDeg = 0f
      var ecuVoltage = 0f
      var mafGs = 0f
      var coolantC = 0
      var fuelRateLph = 0f
      var stftPct = 0f
      var ltftPct = 0f
      var o2B1s2V = 0f
      var o2B1s2TrimPct = 0f
      var engineRunTimeSec = 0
      var milDistanceKm = 0
      var o2S1Ratio = 0f
      var o2S1Voltage = 0f
      var evapPurgePct = 0
      var warmupsSinceCleared = 0
      var distanceSinceClearedKm = 0
      var catalystTempC = 0f
      var absoluteLoadPct = 0f
      var commandedAfr = 0f
      var throttleBPct = 0
      var accelPedalDPct = 0
      var accelPedalEPct = 0
      var fuelType = 0
      var secO2TrimStPct = 0f
      var secO2TrimLtPct = 0f
      var ts = 0L
      var iatC = 0
      var iat2C = 0

      try {
        // ボディをObdBlePacketのフィールド宣言順どおりに1つずつ読み進める。
        rpm = r.u16()
        speedKmh = r.u8()
        loadPct = r.u8()
        mapKpa = r.u8()
        baroKpa = r.u8()
        boostKpa = r.i8()
        throttlePct = r.u8()
        timingDeg = r.f32()
        ecuVoltage = r.f32()
        mafGs = r.f32()
        coolantC = r.i16()
        fuelRateLph = r.f32()
        stftPct = r.f32()
        ltftPct = r.f32()
        o2B1s2V = r.f32()
        o2B1s2TrimPct = r.f32()
        engineRunTimeSec = r.u16()
        milDistanceKm = r.u16()
        o2S1Ratio = r.f32()
        o2S1Voltage = r.f32()
        evapPurgePct = r.u8()
        warmupsSinceCleared = r.u8()
        distanceSinceClearedKm = r.u16()
        catalystTempC = r.f32()
        absoluteLoadPct = r.f32()
        commandedAfr = r.f32()
        throttleBPct = r.u8()
        accelPedalDPct = r.u8()
        accelPedalEPct = r.u8()
        fuelType = r.u8()
        secO2TrimStPct = r.f32()
        secO2TrimLtPct = r.f32()
        ts = r.u32()
        iatC = r.i16()
        iat2C = r.i16()

        // ボディを読み終えた時点でちょうどextOffsetに到達しているはず。setLimit()は
        // 「超えたら」しか検知しないため、逆に「読み足りない」ケースはここで別途拾う。
        if (r.pos != extOffset) {
          valid = false
        }
      } catch (e: ReaderOverrunException) {
        // 境界がズレている場合、それ以前に読んだボディの値も正しいオフセットで読めている
        // 保証がない。壊れた値をvalid=trueのまま返さないよう、ボディ全体を無効扱いにする。
        valid = false
      }

      // TLV拡張フィールド領域: [extCount:1]([fieldId:1][len:1][data:len])×extCount。
      // ボディのパース成否に関わらず独立してパースを試みる。fieldIdの型とlenの申告値が
      // 食い違っている等で範囲外読み取りになっても、ボディのvalid判定には影響させず、
      // その時点までにパースできたTLVフィールドだけを採用して打ち切る。
      var atfTempC = 0
      var atfTempValid = false
      if (bytes.size > extOffset) {
        try {
          val ext = Reader(bytes, extOffset)
          val extCount = ext.u8()
          var i = 0
          while (i < extCount && ext.pos + 2 <= bytes.size) {
            val fieldId = ext.u8()
            val len = ext.u8()
            if (ext.pos + len > bytes.size) break // データ不足（壊れたパケット）、安全に打ち切り
            when (fieldId) {
              EXT_FIELD_ATF_TEMP_C -> atfTempC = ext.i16()
              EXT_FIELD_ATF_TEMP_VALID -> atfTempValid = ext.u8() != 0
              else -> ext.seekTo(ext.pos + len) // 未知のfieldIdはlen分読み飛ばす
            }
            i++
          }
        } catch (e: ReaderOverrunException) {
          // fieldIdが要求するサイズとlenの申告値が食い違っている等、壊れたTLVデータ。
          // ここまでにパースできた分だけを採用して打ち切る。
        }
      }

      return ObdReading(
          rpm = rpm,
          speedKmh = speedKmh,
          loadPct = loadPct,
          mapKpa = mapKpa,
          baroKpa = baroKpa,
          boostKpa = boostKpa,
          throttlePct = throttlePct,
          timingDeg = timingDeg,
          ecuVoltage = ecuVoltage,
          mafGs = mafGs,
          coolantC = coolantC,
          fuelRateLph = fuelRateLph,
          stftPct = stftPct,
          ltftPct = ltftPct,
          o2B1s2V = o2B1s2V,
          o2B1s2TrimPct = o2B1s2TrimPct,
          engineRunTimeSec = engineRunTimeSec,
          milDistanceKm = milDistanceKm,
          o2S1Ratio = o2S1Ratio,
          o2S1Voltage = o2S1Voltage,
          evapPurgePct = evapPurgePct,
          warmupsSinceCleared = warmupsSinceCleared,
          distanceSinceClearedKm = distanceSinceClearedKm,
          catalystTempC = catalystTempC,
          absoluteLoadPct = absoluteLoadPct,
          commandedAfr = commandedAfr,
          throttleBPct = throttleBPct,
          accelPedalDPct = accelPedalDPct,
          accelPedalEPct = accelPedalEPct,
          fuelType = fuelType,
          secO2TrimStPct = secO2TrimStPct,
          secO2TrimLtPct = secO2TrimLtPct,
          valid = valid,
          ts = ts,
          iatC = iatC,
          iat2C = iat2C,
          validMask = validMask,
          atfTempC = atfTempC,
          atfTempValid = atfTempValid,
      )
    }
  }
}
