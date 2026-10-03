package info.karuru.cariot.auth

import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.content.Context
import android.content.Intent
import androidx.core.app.NotificationCompat
import androidx.core.app.NotificationManagerCompat
import info.karuru.cariot.MainActivity

private const val CHANNEL_ID = "car_iot_session_expired"
private const val NOTIFICATION_ID = 3

// ログイン切れでOBDデータを送れなくなったことを通知で知らせる。
// アプリはBLE接続で裏で動くため、画面の「未ログイン」表示だけでは気づけない
// （refresh_token失効に1週間気づかず、走行データが端末に滞留したことがある）。
// アップロード用通知チャンネル(IMPORTANCE_MIN)はステータスバーに出ないため、別チャンネルにする。
object SessionExpiredNotifier {
  fun notify(context: Context, pendingCount: Int) {
    val manager = context.getSystemService(NotificationManager::class.java)
    manager.createNotificationChannel(
        NotificationChannel(CHANNEL_ID, "ログイン切れ", NotificationManager.IMPORTANCE_DEFAULT),
    )
    val openAppIntent = PendingIntent.getActivity(
        context,
        0,
        Intent(context, MainActivity::class.java),
        PendingIntent.FLAG_IMMUTABLE,
    )
    val notification = NotificationCompat.Builder(context, CHANNEL_ID)
        .setContentTitle("ログインが切れています")
        .setContentText("OBDデータを送信できません（未送信 ${pendingCount}件）。アプリを開いてログインしてください")
        .setStyle(
            NotificationCompat.BigTextStyle()
                .bigText("OBDデータを送信できません（未送信 ${pendingCount}件）。アプリを開いてログインしてください"),
        )
        .setSmallIcon(android.R.drawable.stat_notify_error)
        .setContentIntent(openAppIntent)
        .setAutoCancel(true)
        // アップロードは5分ごとに起動するため、表示中は件数の更新だけにして毎回鳴らさない
        .setOnlyAlertOnce(true)
        .build()
    // POST_NOTIFICATIONSが拒否されている場合は何もしない（画面の「未ログイン」表示で気づける）
    if (NotificationManagerCompat.from(context).areNotificationsEnabled()) {
      manager.notify(NOTIFICATION_ID, notification)
    }
  }

  fun cancel(context: Context) {
    context.getSystemService(NotificationManager::class.java).cancel(NOTIFICATION_ID)
  }
}
