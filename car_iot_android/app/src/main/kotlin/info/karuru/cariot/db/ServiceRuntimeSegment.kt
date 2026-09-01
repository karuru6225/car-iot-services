package info.karuru.cariot.db

import androidx.room.Dao
import androidx.room.Entity
import androidx.room.Insert
import androidx.room.PrimaryKey
import androidx.room.Query

const val SERVICE_KIND_FOREGROUND = "FOREGROUND"
const val SERVICE_KIND_UPLOAD = "UPLOAD"

// CarIotForegroundService/CarIotUploadServiceそれぞれの稼働区間。開始時にendTs=nullで
// insertし、正常終了時にendTsをUPDATEする。強制終了された場合はendTs=nullのまま残るが、
// これは集計側（upload/RuntimeSegmentThrottle.kt）が「現在時刻まで稼働中」とみなして安全側に
// 倒すことで対応する（docs/car_iot_android_plan.md）。
@Entity(tableName = "service_runtime_segment")
data class ServiceRuntimeSegment(
    @PrimaryKey(autoGenerate = true) val id: Long = 0,
    val kind: String,
    val startTs: Long,
    val endTs: Long?,
)

@Dao
interface ServiceRuntimeSegmentDao {
  @Insert
  suspend fun insert(segment: ServiceRuntimeSegment): Long

  @Query("UPDATE service_runtime_segment SET endTs = :endTs WHERE id = :id")
  suspend fun close(id: Long, endTs: Long)

  // windowStartより前に完全に終了した区間は24時間ウィンドウの稼働時間計算に寄与しないため、
  // それ以外（実行中=endTs IS NULL、またはendTsがwindowStart以降）だけ取得すれば足りる。
  @Query(
      "SELECT * FROM service_runtime_segment WHERE kind = :kind " +
          "AND (endTs IS NULL OR endTs >= :windowStart)",
  )
  suspend fun segmentsOverlapping(kind: String, windowStart: Long): List<ServiceRuntimeSegment>

  // 集計に二度と使われない完全終了済みの古い区間を間引き、テーブル肥大化を防ぐ。
  @Query("DELETE FROM service_runtime_segment WHERE endTs IS NOT NULL AND endTs < :windowStart")
  suspend fun deleteOlderThan(windowStart: Long)
}
