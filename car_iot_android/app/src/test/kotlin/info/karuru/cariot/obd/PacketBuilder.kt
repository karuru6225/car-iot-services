package info.karuru.cariot.obd

import java.io.ByteArrayOutputStream
import java.nio.ByteBuffer
import java.nio.ByteOrder

// テスト専用: ObdReading.fromBytes()が期待するバイト列（ヘッダ+ボディ+TLV拡張+CRC8）を
// 組み立てるビルダー。ObdBlePacket（esp32_iot_gateway/src/domain/obd.h）のフィールド順・型を
// 忠実に再現する。プロダクションコード（ObdReading.kt）には含めない、テスト専用のヘルパー。
// 全フィールドを個別プロパティとして保持し、build()時にフィールド宣言順どおりに書き込むため、
// zeroBodyPacket()から特定フィールドだけ書き換えてテストできる。
class PacketBuilder {
  var schemaVersion: Int = 1
  var valid: Int = 1
  var validMask: Long = 0xFFFFFFFFL

  var rpm: Int = 0
  var speedKmh: Int = 0
  var loadPct: Int = 0
  var mapKpa: Int = 0
  var baroKpa: Int = 0
  var boostKpa: Int = 0
  var throttlePct: Int = 0
  var timingDeg: Float = 0f
  var ecuVoltage: Float = 0f
  var mafGs: Float = 0f
  var coolantC: Int = 0
  var fuelRateLph: Float = 0f
  var stftPct: Float = 0f
  var ltftPct: Float = 0f
  var o2B1s2V: Float = 0f
  var o2B1s2TrimPct: Float = 0f
  var engineRunTimeSec: Int = 0
  var milDistanceKm: Int = 0
  var o2S1Ratio: Float = 0f
  var o2S1Voltage: Float = 0f
  var evapPurgePct: Int = 0
  var warmupsSinceCleared: Int = 0
  var distanceSinceClearedKm: Int = 0
  var catalystTempC: Float = 0f
  var absoluteLoadPct: Float = 0f
  var commandedAfr: Float = 0f
  var throttleBPct: Int = 0
  var accelPedalDPct: Int = 0
  var accelPedalEPct: Int = 0
  var fuelType: Int = 0
  var secO2TrimStPct: Float = 0f
  var secO2TrimLtPct: Float = 0f
  var ts: Long = 0
  var iatC: Int = 0
  var iat2C: Int = 0

  private val ext = ByteArrayOutputStream()
  private var extCount = 0

  fun extField(fieldId: Int, data: ByteArray) = apply {
    ext.write(fieldId)
    ext.write(data.size)
    ext.write(data)
    extCount++
  }

  // 正しいCRC8付きの完成したバイト列を返す
  fun build(): ByteArray {
    val body = ByteArrayOutputStream()
    body.write(le(2, rpm.toLong()))
    body.write(speedKmh and 0xFF)
    body.write(loadPct and 0xFF)
    body.write(mapKpa and 0xFF)
    body.write(baroKpa and 0xFF)
    body.write(boostKpa and 0xFF)
    body.write(throttlePct and 0xFF)
    body.write(f32(timingDeg))
    body.write(f32(ecuVoltage))
    body.write(f32(mafGs))
    body.write(le(2, coolantC.toLong()))
    body.write(f32(fuelRateLph))
    body.write(f32(stftPct))
    body.write(f32(ltftPct))
    body.write(f32(o2B1s2V))
    body.write(f32(o2B1s2TrimPct))
    body.write(le(2, engineRunTimeSec.toLong()))
    body.write(le(2, milDistanceKm.toLong()))
    body.write(f32(o2S1Ratio))
    body.write(f32(o2S1Voltage))
    body.write(evapPurgePct and 0xFF)
    body.write(warmupsSinceCleared and 0xFF)
    body.write(le(2, distanceSinceClearedKm.toLong()))
    body.write(f32(catalystTempC))
    body.write(f32(absoluteLoadPct))
    body.write(f32(commandedAfr))
    body.write(throttleBPct and 0xFF)
    body.write(accelPedalDPct and 0xFF)
    body.write(accelPedalEPct and 0xFF)
    body.write(fuelType and 0xFF)
    body.write(f32(secO2TrimStPct))
    body.write(f32(secO2TrimLtPct))
    body.write(le(4, ts))
    body.write(le(2, iatC.toLong()))
    body.write(le(2, iat2C.toLong()))
    val bodyBytes = body.toByteArray()

    val headerLen = 8 // schemaVersion(1)+headerLen(1)+extOffset(1)+valid(1)+validMask(4)
    val extOffset = headerLen + bodyBytes.size

    val core = ByteArrayOutputStream()
    core.write(schemaVersion)
    core.write(headerLen)
    core.write(extOffset)
    core.write(valid)
    core.write(le(4, validMask))
    core.write(bodyBytes)

    if (extCount > 0) {
      core.write(extCount)
      core.write(ext.toByteArray())
    }

    val payload = core.toByteArray()
    val crc = crc8(payload)
    return payload + crc.toByte()
  }

  private fun le(size: Int, v: Long): ByteArray {
    val out = ByteArray(size)
    for (i in 0 until size) {
      out[i] = ((v shr (8 * i)) and 0xFF).toByte()
    }
    return out
  }

  private fun f32(v: Float): ByteArray =
      ByteBuffer.allocate(4).order(ByteOrder.LITTLE_ENDIAN).putFloat(v).array()
}

// 全35フィールドをゼロ値で埋めた最小の正常パケットを組み立てる（境界値テストの土台。
// 個々のプロパティを上書きしてからbuild()すれば、そのフィールドだけを差し替えたテストになる）。
fun zeroBodyPacket(): PacketBuilder = PacketBuilder()
