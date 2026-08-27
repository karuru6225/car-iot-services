package info.karuru.cariot.ble

// ESP32側 device/ble_peripheral.cpp の notifyObd() と対になる受信処理。
// [seq:1][total:1][payload] 形式のチャンクを seq==0 から集め、total個揃ったら結合して返す。
// ObdReadingへのパースは呼び出し側の責務（add()の戻り値をObdReading.fromBytes()に渡す）。
class ObdChunkAssembler {
  private val chunks = mutableMapOf<Int, ByteArray>()
  private var total: Int? = null

  fun add(raw: ByteArray): ByteArray? {
    if (raw.size < 2) return null
    val seq = raw[0].toInt() and 0xFF
    val chunkTotal = raw[1].toInt() and 0xFF
    val payload = raw.copyOfRange(2, raw.size)

    if (seq == 0 || total != chunkTotal) {
      chunks.clear()
      total = chunkTotal
    }
    chunks[seq] = payload

    if (chunks.size != chunkTotal) return null

    val result = ByteArray(chunks.values.sumOf { it.size })
    var offset = 0
    for (i in 0 until chunkTotal) {
      val chunk = chunks[i] ?: return null
      chunk.copyInto(result, offset)
      offset += chunk.size
    }
    chunks.clear()
    return result
  }

  fun reset() {
    chunks.clear()
    total = null
  }
}
