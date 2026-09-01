package info.karuru.cariot.service

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.Service
import android.content.Intent
import android.os.Build
import android.os.IBinder
import android.util.Log
import androidx.annotation.RequiresApi
import androidx.core.app.NotificationCompat
import info.karuru.cariot.auth.AuthStore
import info.karuru.cariot.db.CarIotDatabase
import info.karuru.cariot.db.SERVICE_KIND_FOREGROUND
import info.karuru.cariot.db.SERVICE_KIND_UPLOAD
import info.karuru.cariot.db.ServiceRuntimeSegment
import info.karuru.cariot.db.ServiceRuntimeSegmentDao
import info.karuru.cariot.state.CarIotState
import info.karuru.cariot.upload.ObdUploader
import info.karuru.cariot.upload.RuntimeSegment
import info.karuru.cariot.upload.RuntimeSegmentThrottle
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.launch

private const val TAG = "CarIotUploadService"
private const val CHANNEL_ID = "car_iot_upload_service"
private const val NOTIFICATION_ID = 2
private const val RUNTIME_WINDOW_MS = 24 * 60 * 60 * 1000L

// OBDアップロードのみを担当するdataSync型Foreground Service。BLE接続を維持する
// CarIotForegroundService（connectedDevice型）とは責務・型を分離する（docs/car_iot_android_plan.md:
// dataSync型の処理をconnectedDevice型に隠すのはOSの制約の趣旨を無視した設計のため不採用とした）。
// 起動のたびにスロットリング判定→未送信バッチを尽きるまで送信→stopSelf()する一回限りの処理で、
// CarIotForegroundServiceと違って常駐しない。
class CarIotUploadService : Service() {
  private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)

  override fun onCreate() {
    super.onCreate()
    createNotificationChannel()
    startForeground(NOTIFICATION_ID, buildNotification())
  }

  override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
    scope.launch { runUpload(startId) }
    return START_NOT_STICKY
  }

  override fun onBind(intent: Intent?): IBinder? = null

  // dataSync型の24時間累積6時間上限に到達した場合のフェイルセーフ。
  // RuntimeSegmentThrottleのスロットリング判定をすり抜けた場合の保険（docs/car_iot_android_plan.md）。
  @RequiresApi(Build.VERSION_CODES.VANILLA_ICE_CREAM)
  override fun onTimeout(startId: Int, fgsType: Int) {
    Log.w(TAG, "6時間上限に到達したため強制終了")
    stopSelf(startId)
  }

  override fun onDestroy() {
    // 実行中のrunUpload()が完了する前にここへ来た場合、ServiceRuntimeSegmentのendTsは
    // nullのまま残る。集計側(RuntimeSegmentThrottle)がendTs=nullをnowまで稼働中とみなし
    // 安全側に倒す設計のため、ここで無理にcloseしようとする必要はない。
    scope.cancel()
    super.onDestroy()
  }

  private suspend fun runUpload(startId: Int) {
    val db = CarIotDatabase.getInstance(applicationContext)
    val segmentDao = db.serviceRuntimeSegmentDao()
    val now = System.currentTimeMillis()
    val windowStart = now - RUNTIME_WINDOW_MS

    val uploadRuntimeMs = runtimeMs(segmentDao, SERVICE_KIND_UPLOAD, windowStart, now)
    val foregroundRuntimeMs = runtimeMs(segmentDao, SERVICE_KIND_FOREGROUND, windowStart, now)
    if (RuntimeSegmentThrottle.shouldSkipUpload(uploadRuntimeMs, foregroundRuntimeMs)) {
      Log.w(TAG, "アップロード見送り: 直近24時間の稼働時間比率が閾値を超過")
      stopSelf(startId)
      return
    }

    segmentDao.deleteOlderThan(windowStart)
    val segmentId = segmentDao.insert(
        ServiceRuntimeSegment(kind = SERVICE_KIND_UPLOAD, startTs = now, endTs = null),
    )

    val deviceId = CarIotState.deviceName.value
    if (deviceId.isNotEmpty()) {
      val uploader = ObdUploader(db.pendingObdReadingDao(), AuthStore(applicationContext))
      while (uploader.uploadOnce(deviceId)) {
        // nextBatch()の上限(100件)を超えるバッファが残っていた場合に備え、
        // 送信対象が尽きるまで繰り返す。
      }
    }

    segmentDao.close(segmentId, System.currentTimeMillis())
    stopSelf(startId)
  }

  private suspend fun runtimeMs(
      dao: ServiceRuntimeSegmentDao,
      kind: String,
      windowStart: Long,
      now: Long,
  ): Long {
    val segments = dao.segmentsOverlapping(kind, windowStart)
        .map { RuntimeSegment(it.startTs, it.endTs) }
    return RuntimeSegmentThrottle.overlappingRuntimeMs(segments, windowStart, now)
  }

  private fun createNotificationChannel() {
    val manager = getSystemService(NotificationManager::class.java)
    val channel = NotificationChannel(
        CHANNEL_ID,
        "OBDアップロード",
        NotificationManager.IMPORTANCE_MIN,
    )
    manager.createNotificationChannel(channel)
  }

  private fun buildNotification(): Notification {
    return NotificationCompat.Builder(this, CHANNEL_ID)
        .setContentTitle("car-iot")
        .setContentText("OBDデータをアップロード中")
        .setSmallIcon(android.R.drawable.stat_sys_upload)
        .setOngoing(true)
        .build()
  }
}
