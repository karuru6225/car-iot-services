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
import info.karuru.cariot.MainActivity
import info.karuru.cariot.ble.BleConnectionManager

private const val CHANNEL_ID = "car_iot_ble_service"
private const val NOTIFICATION_ID = 1

const val ACTION_CONNECT = "info.karuru.cariot.action.CONNECT"
const val ACTION_DISCONNECT = "info.karuru.cariot.action.DISCONNECT"

// BLE接続・OBD受信を担当するForeground Service（connectedDevice型）。
// アプリ(Activity)が破棄されていてもこのServiceが生きている限りBLE接続・データ収集を続ける
// （docs/car_iot_android_plan.md Phase 3の核心）。UI→ServiceのコマンドはstartForegroundService()の
// Intent actionで送る（Binderバインドは使わない）。
class CarIotForegroundService : Service() {
  private lateinit var bleManager: BleConnectionManager
  private var wakeLock: PowerManager.WakeLock? = null

  override fun onCreate() {
    super.onCreate()
    bleManager = BleConnectionManager(applicationContext)
    createNotificationChannel()
    // Android 12+でのANR回避のため、onCreate直後に仮の通知でstartForegroundしておく
    // （接続状態が変わったらNotificationManager.notify()で更新する）。
    startForeground(NOTIFICATION_ID, buildNotification())
    acquireWakeLock()
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
    releaseWakeLock()
    super.onDestroy()
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
