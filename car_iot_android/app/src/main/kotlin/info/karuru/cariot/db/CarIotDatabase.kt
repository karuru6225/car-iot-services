package info.karuru.cariot.db

import android.content.Context
import androidx.room.Database
import androidx.room.Room
import androidx.room.RoomDatabase

// PendingObdReading（アップロード待ちOBDデータ）とServiceRuntimeSegment（稼働時間ログ）を
// 同じDBに同居させる（依存はRoom 1つで済む、docs/car_iot_android_plan.md）。
// exportSchema=false: マイグレーション履歴の厳密な管理はまだ必要ないため
// （room.schemaLocation未設定でのビルド警告を避ける）。
@Database(
    entities = [PendingObdReading::class, ServiceRuntimeSegment::class],
    version = 1,
    exportSchema = false,
)
abstract class CarIotDatabase : RoomDatabase() {
  abstract fun pendingObdReadingDao(): PendingObdReadingDao
  abstract fun serviceRuntimeSegmentDao(): ServiceRuntimeSegmentDao

  companion object {
    @Volatile private var instance: CarIotDatabase? = null

    fun getInstance(context: Context): CarIotDatabase {
      return instance ?: synchronized(this) {
        instance ?: Room.databaseBuilder(
            context.applicationContext,
            CarIotDatabase::class.java,
            "car_iot.db",
        ).build().also { instance = it }
      }
    }
  }
}
