package info.karuru.cariot.upload

import android.content.Context

private const val PREFS_NAME = "upload_status_prefs"
private const val KEY_LAST_DEVICE_ID = "last_device_id"
private const val KEY_LAST_TS = "last_upload_ts"
private const val KEY_LAST_OUTCOME = "last_upload_outcome"
private const val KEY_LAST_SENT = "last_upload_sent"

// 1回のアップロード起動（CarIotUploadService.runUpload）の結末。データタブに表示する。
enum class UploadOutcome(val label: String) {
  // 未送信が尽きるまで送れた（送信0件で終わった場合も含む）
  DONE("完了"),
  FAILED("失敗（通信エラー等）"),
  AUTH_REQUIRED("ログイン切れ"),
  // BLE未接続かつ一度も接続したことがなく、送信先のdevice_idが分からない
  NO_DEVICE("送信先デバイス不明"),
  THROTTLED("見送り（稼働時間の上限）"),
}

data class UploadStatus(val ts: Long, val outcome: UploadOutcome, val sentCount: Int)

// 最後のアップロード結果と、最後に接続したESP32のデバイス名の永続化。
// デバイス名はAPIのdevice_idになる。BLE接続中しか分からない値なので、未接続のときに
// データタブから手動送信できるよう、接続のたびに保存しておく。
// ui/theme/ThemeStore.ktと同じSharedPreferencesパターン。
class UploadStatusStore(context: Context) {
  private val prefs = context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)

  fun lastDeviceId(): String = prefs.getString(KEY_LAST_DEVICE_ID, null).orEmpty()

  fun saveDeviceId(deviceId: String) {
    if (deviceId.isEmpty()) return
    prefs.edit().putString(KEY_LAST_DEVICE_ID, deviceId).apply()
  }

  fun loadStatus(): UploadStatus? {
    val ts = prefs.getLong(KEY_LAST_TS, 0L)
    if (ts == 0L) return null
    val outcome = try {
      UploadOutcome.valueOf(prefs.getString(KEY_LAST_OUTCOME, null) ?: return null)
    } catch (e: IllegalArgumentException) {
      return null
    }
    return UploadStatus(ts, outcome, prefs.getInt(KEY_LAST_SENT, 0))
  }

  fun saveStatus(status: UploadStatus) {
    prefs.edit()
        .putLong(KEY_LAST_TS, status.ts)
        .putString(KEY_LAST_OUTCOME, status.outcome.name)
        .putInt(KEY_LAST_SENT, status.sentCount)
        .apply()
  }
}
