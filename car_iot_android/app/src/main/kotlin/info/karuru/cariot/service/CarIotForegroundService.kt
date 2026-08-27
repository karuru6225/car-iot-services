package info.karuru.cariot.service

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.os.IBinder
import android.os.PowerManager
import androidx.core.app.NotificationCompat
import androidx.core.content.ContextCompat
import info.karuru.cariot.MainActivity
import info.karuru.cariot.ble.BleConnectionManager
import info.karuru.cariot.ble.ConnState
import info.karuru.cariot.db.CarIotDatabase
import info.karuru.cariot.db.PendingObdReading
import info.karuru.cariot.db.SERVICE_KIND_FOREGROUND
import info.karuru.cariot.db.ServiceRuntimeSegment
import info.karuru.cariot.location.LocationTracker
import info.karuru.cariot.obd.ObdReading
import info.karuru.cariot.state.CarIotState
import info.karuru.cariot.upload.PENDING_READING_HARD_CAP
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.runBlocking

private const val CHANNEL_ID = "car_iot_ble_service"
private const val NOTIFICATION_ID = 1
private const val UPLOAD_INTERVAL_MS = 5 * 60 * 1000L

const val ACTION_CONNECT = "info.karuru.cariot.action.CONNECT"
const val ACTION_DISCONNECT = "info.karuru.cariot.action.DISCONNECT"

// BLE接続・OBD受信を担当するForeground Service（connectedDevice型）。
// アプリ(Activity)が破棄されていてもこのServiceが生きている限りBLE接続・データ収集を続ける
// （docs/car_iot_android_plan.md Phase 3の核心）。UI→ServiceのコマンドはstartForegroundService()の
// Intent actionで送る（Binderバインドは使わない）。
// Phase5: 受信したOBDデータをRoomへ永続化し、5分間隔（＋接続直後の即時1回、＋valid=falseへ
// 変わった直後の尾流し）でCarIotUploadServiceを起動してアップロードをトリガーする。
class CarIotForegroundService : Service() {
  private lateinit var bleManager: BleConnectionManager
  private lateinit var locationTracker: LocationTracker
  private var wakeLock: PowerManager.WakeLock? = null
  private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
  private val db by lazy { CarIotDatabase.getInstance(applicationContext) }
  private var segmentId: Long? = null
  private var uploadTimerJob: Job? = null
  private var lastValid = true

  override fun onCreate() {
    super.onCreate()
    bleManager = BleConnectionManager(applicationContext, onObdReading = ::onObdReadingReceived)
    locationTracker = LocationTracker(applicationContext)
    createNotificationChannel()
    // Android 12+でのANR回避のため、onCreate直後に仮の通知でstartForegroundしておく
    // （接続状態が変わったらNotificationManager.notify()で更新する）。
    startForeground(NOTIFICATION_ID, buildNotification())
    acquireWakeLock()
    scope.launch {
      segmentId = db.serviceRuntimeSegmentDao().insert(
          ServiceRuntimeSegment(
              kind = SERVICE_KIND_FOREGROUND,
              startTs = System.currentTimeMillis(),
              endTs = null,
          ),
      )
    }
    observeConnState()
  }

  override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
    when (intent?.action) {
      ACTION_CONNECT -> bleManager.connect()
      ACTION_DISCONNECT -> bleManager.disconnect()
    }
    return START_STICKY
  }

  override fun onBind(intent: Intent?): IBinder? = null

  override fun onDestroy() {
    bleManager.disconnect()
    locationTracker.stop()
    releaseWakeLock()
    // 正常終了時はここでendTsを確定させる。runBlocking()はSQLite1行のUPDATEのみで
    // 短時間のため許容する（onDestroy()はsuspend関数ではないため）。ここに来る前に
    // プロセスごと強制終了された場合はendTs=nullのまま残るが、CarIotUploadService側の
    // 集計がnowまで稼働中とみなして安全側に倒す（docs/car_iot_android_plan.md）。
    segmentId?.let { id ->
      runBlocking { db.serviceRuntimeSegmentDao().close(id, System.currentTimeMillis()) }
    }
    scope.cancel()
    super.onDestroy()
  }

  // BLE接続確立中だけアップロードタイマーと位置情報取得を行う。接続直後は検証をしやすく
  // するため5分待たず即座に1回トリガーする。
  private fun observeConnState() {
    scope.launch {
      CarIotState.connState.collect { state ->
        if (state == ConnState.CONNECTED) {
          locationTracker.start()
          if (uploadTimerJob == null) {
            uploadTimerJob = scope.launch {
              triggerUpload()
              while (isActive) {
                delay(UPLOAD_INTERVAL_MS)
                triggerUpload()
              }
            }
          }
        } else {
          locationTracker.stop()
          uploadTimerJob?.cancel()
          uploadTimerJob = null
        }
      }
    }
  }

  private fun onObdReadingReceived(reading: ObdReading) {
    // 位置情報取得の成否とOBD受信は独立させる（権限未許可・GPS未捕捉時はlat/lon=nullのまま
    // 送信され、Lambda側もキーが無ければGPS未取得として扱う、docs/car_iot_android_plan.md）。
    val location = locationTracker.lastLocation
    scope.launch {
      db.pendingObdReadingDao().insertAndTrim(
          PendingObdReading.from(reading, lat = location?.latitude, lon = location?.longitude),
          PENDING_READING_HARD_CAP,
      )
    }
    if (lastValid && !reading.valid) {
      // valid=falseへ変わった直後は5分待たず即座にアップロードする（尾流し。
      // クラウド側でIGN OFFをできるだけ早く検知できるようにするため、
      // docs/car_iot_android_plan.md）。
      triggerUpload()
    }
    lastValid = reading.valid
  }

  private fun triggerUpload() {
    ContextCompat.startForegroundService(this, Intent(this, CarIotUploadService::class.java))
  }

  private fun createNotificationChannel() {
    val manager = getSystemService(NotificationManager::class.java)
    val channel = NotificationChannel(
        CHANNEL_ID,
        "BLE接続状態",
        NotificationManager.IMPORTANCE_LOW,
    )
    manager.createNotificationChannel(channel)
  }

  private fun buildNotification(): Notification {
    val openAppIntent = PendingIntent.getActivity(
        this,
        0,
        Intent(this, MainActivity::class.java),
        PendingIntent.FLAG_IMMUTABLE,
    )
    return NotificationCompat.Builder(this, CHANNEL_ID)
        .setContentTitle("car-iot")
        .setContentText("BLE接続を待機中")
        .setSmallIcon(android.R.drawable.stat_sys_data_bluetooth)
        .setContentIntent(openAppIntent)
        .setOngoing(true)
        .build()
  }

  private fun acquireWakeLock() {
    val powerManager = getSystemService(Context.POWER_SERVICE) as PowerManager
    wakeLock = powerManager.newWakeLock(
        PowerManager.PARTIAL_WAKE_LOCK,
        "CarIot::BleConnectionWakeLock",
    ).apply { acquire() }
  }

  private fun releaseWakeLock() {
    wakeLock?.let { if (it.isHeld) it.release() }
    wakeLock = null
  }
}
