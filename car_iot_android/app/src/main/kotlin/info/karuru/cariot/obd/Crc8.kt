package info.karuru.cariot.obd

// esp32_iot_gateway/src/domain/obd.cpp の obdCrc8() と同一アルゴリズム（多項式0x07、初期値0x00）。
fun crc8(bytes: ByteArray, len: Int = bytes.size): Int {
  var crc = 0x00
  for (i in 0 until len) {
    crc = crc xor (bytes[i].toInt() and 0xFF)
    repeat(8) {
      crc = if (crc and 0x80 != 0) {
        ((crc shl 1) xor 0x07) and 0xFF
      } else {
        (crc shl 1) and 0xFF
      }
    }
  }
  return crc
}
