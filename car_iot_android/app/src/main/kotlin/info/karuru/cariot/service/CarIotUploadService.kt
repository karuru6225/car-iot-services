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
import info.karuru.cariot.auth.SessionExpiredNotifier
import info.karuru.cariot.db.CarIotDatabase
import info.karuru.cariot.db.SERVICE_KIND_FOREGROUND
import info.karuru.cariot.db.SERVICE_KIND_UPLOAD
import info.karuru.cariot.db.ServiceRuntimeSegment
import info.karuru.cariot.db.ServiceRuntimeSegmentDao
import info.karuru.cariot.state.CarIotState
import info.karuru.cariot.upload.ObdUploader
import info.karuru.cariot.upload.RuntimeSegment
import info.karuru.cariot.upload.RuntimeSegmentThrottle
import info.karuru.cariot.upload.UploadOutcome
import info.karuru.cariot.upload.UploadResult
import info.karuru.cariot.upload.UploadStatus
import info.karuru.cariot.upload.UploadStatusStore
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.launch

private const val TAG = "CarIotUploadService"
private const val CHANNEL_ID = "car_iot_upload_service"
private const val NOTIFICATION_ID = 2
private const val RUNTIME_WINDOW_MS = 24 * 60 * 60 * 1000L

// データタブの「今すぐ送信」から起動されたことを示すIntent extra。
const val EXTRA_MANUAL_UPLOAD = "info.karuru.cariot.extra.MANUAL_UPLOAD"

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
    val manual = intent?.getBooleanExtra(EXTRA_MANUAL_UPLOAD, false) ?: false
    scope.launch { runUpload(startId, manual) }
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

  private suspend fun runUpload(startId: Int, manual: Boolean) {
    CarIotState.setUploading(true)
    try {
      runUploadInner(startId, manual)
    } finally {
      CarIotState.setUploading(false)
    }
  }

  private suspend fun runUploadInner(startId: Int, manual: Boolean) {
    val db = CarIotDatabase.getInstance(applicationContext)
    val statusStore = UploadStatusStore(applicationContext)
    val segmentDao = db.serviceRuntimeSegmentDao()
    val now = System.currentTimeMillis()
    val windowStart = now - RUNTIME_WINDOW_MS

    val uploadRuntimeMs = runtimeMs(segmentDao, SERVICE_KIND_UPLOAD, windowStart, now)
    val foregroundRuntimeMs = runtimeMs(segmentDao, SERVICE_KIND_FOREGROUND, windowStart, now)
    // 手動送信はユーザーが画面を見ながら1回押すものなので比率判定をかけない。BLE未接続中は
    // FOREGROUND側の稼働が0になり、比率判定では必ず見送りになってしまうため。
    // 6時間上限のフェイルセーフ(onTimeout)は手動送信でも効く。
    if (!manual && RuntimeSegmentThrottle.shouldSkipUpload(uploadRuntimeMs, foregroundRuntimeMs)) {
      Log.w(TAG, "アップロード見送り: 直近24時間の稼働時間比率が閾値を超過")
      recordStatus(statusStore, UploadOutcome.THROTTLED, 0)
      stopSelf(startId)
      return
    }

    segmentDao.deleteOlderThan(windowStart)
    val segmentId = segmentDao.insert(
        ServiceRuntimeSegment(kind = SERVICE_KIND_UPLOAD, startTs = now, endTs = null),
    )

    // BLE接続中はそのデバイス名を使い、次回の未接続時の手動送信用に保存しておく。
    // 未接続（手動送信）なら最後に接続したデバイス名を使う。
    val connectedName = CarIotState.deviceName.value
    statusStore.saveDeviceId(connectedName)
    val deviceId = connectedName.ifEmpty { statusStore.lastDeviceId() }
    if (deviceId.isEmpty()) {
      Log.w(TAG, "アップロード不可: 送信先device_idが不明（ESP32に一度も接続していない）")
      recordStatus(statusStore, UploadOutcome.NO_DEVICE, 0)
    } else {
      val pendingDao = db.pendingObdReadingDao()
      val authStore = AuthStore(applicationContext)
      val uploader = ObdUploader(pendingDao, authStore)
      // nextBatch()の上限(100件)を超えるバッファが残っていた場合に備え、送信に成功している
      // 間は送信対象が尽きるまで繰り返す。失敗したら同じバッチを即再送せず次回の起動に任せる。
      var result: UploadResult
      do {
        result = uploader.uploadOnce(deviceId)
      } while (result == UploadResult.SENT)

      if (result == UploadResult.AUTH_REQUIRED) {
        // 失効したトークンを残すと画面は「ログイン: ...」のまま送信だけ止まり続けるため、
        // ログアウトと同じ状態にして画面にも「未ログイン」とログインボタンを出す
        authStore.clear()
        CarIotState.setUserEmail(null)
        SessionExpiredNotifier.notify(applicationContext, pendingDao.count())
      }
      val outcome = when (result) {
        UploadResult.AUTH_REQUIRED -> UploadOutcome.AUTH_REQUIRED
        UploadResult.FAILED -> UploadOutcome.FAILED
        else -> UploadOutcome.DONE
      }
      recordStatus(statusStore, outcome, uploader.sentCount)
    }

    segmentDao.close(segmentId, System.currentTimeMillis())
    stopSelf(startId)
  }

  private fun recordStatus(store: UploadStatusStore, outcome: UploadOutcome, sentCount: Int) {
    val status = UploadStatus(System.currentTimeMillis(), outcome, sentCount)
    store.saveStatus(status)
    CarIotState.setUploadStatus(status)
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
