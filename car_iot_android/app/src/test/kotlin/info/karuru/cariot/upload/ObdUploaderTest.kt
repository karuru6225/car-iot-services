package info.karuru.cariot.upload

import info.karuru.cariot.db.PendingObdReading
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.boolean
import kotlinx.serialization.json.double
import kotlinx.serialization.json.int
import kotlinx.serialization.json.jsonArray
import kotlinx.serialization.json.jsonObject
import kotlinx.serialization.json.jsonPrimitive
import kotlinx.serialization.json.long
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

private fun sampleReading(id: Long = 1, lat: Double? = null, lon: Double? = null) = PendingObdReading(
    id = id,
    ts = 1735689600,
    rpm = 1800,
    speedKmh = 45,
    loadPct = 30,
    mapKpa = 95,
    baroKpa = 101,
    boostKpa = -2,
    throttlePct = 20,
    timingDeg = 12.5f,
    ecuVoltage = 14.1f,
    mafGs = 8.2f,
    coolantC = 88,
    fuelRateLph = 3.4f,
    stftPct = 1.2f,
    ltftPct = -0.5f,
    o2B1s2V = 0.7f,
    o2B1s2TrimPct = 0.0f,
    engineRunTimeSec = 600,
    milDistanceKm = 0,
    o2S1Ratio = 1.0f,
    o2S1Voltage = 0.45f,
    evapPurgePct = 0,
    warmupsSinceCleared = 2,
    distanceSinceClearedKm = 120,
    catalystTempC = 400.0f,
    absoluteLoadPct = 28.0f,
    commandedAfr = 14.7f,
    throttleBPct = 0,
    accelPedalDPct = 15,
    accelPedalEPct = 15,
    fuelType = 1,
    secO2TrimStPct = 0.0f,
    secO2TrimLtPct = 0.0f,
    iatC = 25,
    iat2C = 26,
    valid = true,
    lat = lat,
    lon = lon,
)

class ObdUploaderTest {
  @Test
  fun `device_idとreadingsがトップレベルに含まれる`() {
    val body = ObdUploader.buildRequestBody("car-iot-abc123", listOf(sampleReading()))
    val root = Json.parseToJsonElement(body).jsonObject
    assertEquals("car-iot-abc123", root["device_id"]!!.jsonPrimitive.content)
    assertEquals(1, root["readings"]!!.jsonArray.size)
  }

  @Test
  fun `readingの各フィールドが正しい値でJSON化される`() {
    val body = ObdUploader.buildRequestBody("dev", listOf(sampleReading()))
    val reading = Json.parseToJsonElement(body).jsonObject["readings"]!!.jsonArray[0].jsonObject
    assertEquals(1735689600L, reading["ts"]!!.jsonPrimitive.long)
    assertEquals(1800, reading["rpm"]!!.jsonPrimitive.int)
    assertEquals(45, reading["speedKmh"]!!.jsonPrimitive.int)
    assertEquals(true, reading["valid"]!!.jsonPrimitive.boolean)
  }

  @Test
  fun `latとlonがnullならキー自体が出力されない`() {
    val body = ObdUploader.buildRequestBody("dev", listOf(sampleReading(lat = null, lon = null)))
    val reading = Json.parseToJsonElement(body).jsonObject["readings"]!!.jsonArray[0].jsonObject
    assertFalse(reading.containsKey("lat"))
    assertFalse(reading.containsKey("lon"))
  }

  @Test
  fun `latとlonに値があればキーと値が出力される`() {
    val body = ObdUploader.buildRequestBody("dev", listOf(sampleReading(lat = 35.681, lon = 139.767)))
    val reading = Json.parseToJsonElement(body).jsonObject["readings"]!!.jsonArray[0].jsonObject
    assertTrue(reading.containsKey("lat"))
    assertEquals(35.681, reading["lat"]!!.jsonPrimitive.double, 0.0001)
    assertEquals(139.767, reading["lon"]!!.jsonPrimitive.double, 0.0001)
  }

  @Test
  fun `Room主キーidはJSONに含まれない`() {
    val body = ObdUploader.buildRequestBody("dev", listOf(sampleReading(id = 42)))
    val reading = Json.parseToJsonElement(body).jsonObject["readings"]!!.jsonArray[0].jsonObject
    assertFalse(reading.containsKey("id"))
  }
}
