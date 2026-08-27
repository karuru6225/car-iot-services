package info.karuru.cariot.db

import androidx.room.Dao
import androidx.room.Entity
import androidx.room.Insert
import androidx.room.PrimaryKey
import androidx.room.Query
import androidx.room.Transaction
import info.karuru.cariot.obd.ObdReading

// AWSアップロード待ちのOBD-IIデータ。infra/lambda_src/obd_ingest/index.pyの_FIELD_MAPが
// 受理するフィールドのみを保持する（atfTempC等の拡張フィールドはLambda未対応のため対象外）。
// アップロード成功でDELETE、失敗時は残して次回再送する（mobile/lib/services/obd_uploader.dartと
// 異なりRoomで永続化し、Service強制終了後も引き継げるようにする、docs/car_iot_android_plan.md）。
@Entity(tableName = "pending_obd_reading")
data class PendingObdReading(
    @PrimaryKey(autoGenerate = true) val id: Long = 0,
    val ts: Long,
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
    val iatC: Int,
    val iat2C: Int,
    val valid: Boolean,
    // GPS未取得時はnull（lat/lon運用はPhase6、Lambda側もキー自体が無い場合を許容する）
    val lat: Double?,
    val lon: Double?,
) {
  companion object {
    fun from(reading: ObdReading, lat: Double?, lon: Double?): PendingObdReading = PendingObdReading(
        ts = reading.ts,
        rpm = reading.rpm,
        speedKmh = reading.speedKmh,
        loadPct = reading.loadPct,
        mapKpa = reading.mapKpa,
        baroKpa = reading.baroKpa,
        boostKpa = reading.boostKpa,
        throttlePct = reading.throttlePct,
        timingDeg = reading.timingDeg,
        ecuVoltage = reading.ecuVoltage,
        mafGs = reading.mafGs,
        coolantC = reading.coolantC,
        fuelRateLph = reading.fuelRateLph,
        stftPct = reading.stftPct,
        ltftPct = reading.ltftPct,
        o2B1s2V = reading.o2B1s2V,
        o2B1s2TrimPct = reading.o2B1s2TrimPct,
        engineRunTimeSec = reading.engineRunTimeSec,
        milDistanceKm = reading.milDistanceKm,
        o2S1Ratio = reading.o2S1Ratio,
        o2S1Voltage = reading.o2S1Voltage,
        evapPurgePct = reading.evapPurgePct,
        warmupsSinceCleared = reading.warmupsSinceCleared,
        distanceSinceClearedKm = reading.distanceSinceClearedKm,
        catalystTempC = reading.catalystTempC,
        absoluteLoadPct = reading.absoluteLoadPct,
        commandedAfr = reading.commandedAfr,
        throttleBPct = reading.throttleBPct,
        accelPedalDPct = reading.accelPedalDPct,
        accelPedalEPct = reading.accelPedalEPct,
        fuelType = reading.fuelType,
        secO2TrimStPct = reading.secO2TrimStPct,
        secO2TrimLtPct = reading.secO2TrimLtPct,
        iatC = reading.iatC,
        iat2C = reading.iat2C,
        valid = reading.valid,
        lat = lat,
        lon = lon,
    )
  }
}

@Dao
interface PendingObdReadingDao {
  @Insert
  suspend fun insert(reading: PendingObdReading): Long

  @Query("SELECT * FROM pending_obd_reading ORDER BY id ASC LIMIT :limit")
  suspend fun nextBatch(limit: Int): List<PendingObdReading>

  @Query("DELETE FROM pending_obd_reading WHERE id IN (:ids)")
  suspend fun deleteByIds(ids: List<Long>)

  @Query("SELECT COUNT(*) FROM pending_obd_reading")
  suspend fun count(): Int

  @Query("SELECT id FROM pending_obd_reading ORDER BY id ASC LIMIT :limit")
  suspend fun oldestIds(limit: Int): List<Long>

  // 送信失敗が続いてバッファが際限なく肥大化しないよう、insertのたびにhardCap超過分を
  // 古い順に間引く（mobile/lib/services/obd_uploader.dartの_maxBufferHardCap相当）。
  @Transaction
  suspend fun insertAndTrim(reading: PendingObdReading, hardCap: Int) {
    insert(reading)
    val over = count() - hardCap
    if (over > 0) {
      deleteByIds(oldestIds(over))
    }
  }
}
