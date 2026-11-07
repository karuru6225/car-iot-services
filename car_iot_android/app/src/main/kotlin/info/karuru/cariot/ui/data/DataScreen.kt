package info.karuru.cariot.ui.data

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import info.karuru.cariot.db.CarIotDatabase
import info.karuru.cariot.db.PendingSummary
import info.karuru.cariot.state.CarIotState
import info.karuru.cariot.ui.common.SectionCard
import info.karuru.cariot.ui.meter.AutoSizeValueText
import info.karuru.cariot.ui.theme.ClusterButtonShape
import info.karuru.cariot.upload.UploadOutcome
import info.karuru.cariot.upload.UploadStatusStore
import java.time.Instant
import java.time.ZoneId
import java.time.format.DateTimeFormatter

private val DATE_TIME_FORMAT: DateTimeFormatter =
    DateTimeFormatter.ofPattern("MM/dd HH:mm").withZone(ZoneId.systemDefault())

// 「データ」タブ。端末に溜まっている未送信OBDデータの件数・期間と、最後のアップロード結果を
// 表示し、BLE未接続でも手動で送信できるようにする。
//
// 自動送信はBLE接続中（走行中）しか動かないため、走行中に送れなかった分は次に乗るまで
// 端末に残り続ける。ログイン切れで1日分が滞留し、状況を確認する手段がadbしか無かった
// ことから追加した。
@Composable
fun DataScreen(onUploadNow: () -> Unit) {
  val context = LocalContext.current
  val dao = remember { CarIotDatabase.getInstance(context).pendingObdReadingDao() }
  val summary by dao.observeSummary()
      .collectAsStateWithLifecycle(initialValue = PendingSummary(0, null, null))
  val userEmail by CarIotState.userEmail.collectAsStateWithLifecycle()
  val uploading by CarIotState.uploading.collectAsStateWithLifecycle()
  val uploadStatus by CarIotState.uploadStatus.collectAsStateWithLifecycle()
  val connectedName by CarIotState.deviceName.collectAsStateWithLifecycle()
  // 保存済みデバイス名は送信のたびに更新されうるので、結果が変わったら読み直す
  val savedDeviceId = remember(uploadStatus) { UploadStatusStore(context).lastDeviceId() }
  val deviceId = connectedName.ifEmpty { savedDeviceId }

  // 送れない理由。複数当てはまる場合はユーザーが先に解消すべきものを出す
  val blocker = when {
    userEmail == null -> "ログインすると送信できます（接続タブ）"
    deviceId.isEmpty() -> "ESP32に一度接続すると送信できます"
    summary.count == 0 -> "未送信のデータはありません"
    else -> null
  }

  Column(
      modifier = Modifier
          .fillMaxWidth()
          .verticalScroll(rememberScrollState())
          .padding(20.dp),
  ) {
    SectionCard(title = "未送信データ") {
      Row(verticalAlignment = Alignment.Bottom) {
        AutoSizeValueText(
            text = "%,d".format(summary.count),
            style = MaterialTheme.typography.displayMedium,
            color = MaterialTheme.colorScheme.onSurface,
            modifier = Modifier.weight(1f, fill = false),
        )
        // 単位は数値に従属する情報なので極小・くすませてベースライン付近に置く（DESIGN.md 3章）
        Text(
            "件",
            style = MaterialTheme.typography.labelLarge,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            modifier = Modifier.padding(start = 6.dp, bottom = 6.dp),
        )
      }
      Spacer(Modifier.height(8.dp))
      InfoRow("期間", summary.periodText())
    }

    SectionCard(title = "アップロード") {
      InfoRow("送信先", deviceId.ifEmpty { "未記録" })
      Spacer(Modifier.height(8.dp))
      InfoRow("最終送信", uploadStatus?.let { s ->
        val sent = if (s.outcome == UploadOutcome.DONE || s.sentCount > 0) " ${s.sentCount}件" else ""
        "${formatMillis(s.ts)}  ${s.outcome.label}$sent"
      } ?: "記録なし")
      Spacer(Modifier.height(16.dp))
      Button(
          onClick = onUploadNow,
          enabled = blocker == null && !uploading,
          shape = ClusterButtonShape,
      ) {
        Text(if (uploading) "送信中..." else "今すぐ送信")
      }
      if (blocker != null) {
        Spacer(Modifier.height(8.dp))
        Text(
            blocker,
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
      }
    }
  }
}

@Composable
private fun InfoRow(label: String, value: String) {
  Column(verticalArrangement = Arrangement.spacedBy(2.dp)) {
    Text(
        label,
        style = MaterialTheme.typography.labelSmall,
        color = MaterialTheme.colorScheme.onSurfaceVariant,
    )
    Text(value, style = MaterialTheme.typography.bodyLarge)
  }
}

private fun PendingSummary.periodText(): String {
  val oldest = oldestTs ?: return "—"
  val newest = newestTs ?: oldest
  return "${formatSeconds(oldest)} 〜 ${formatSeconds(newest)}"
}

private fun formatSeconds(epochSec: Long): String = DATE_TIME_FORMAT.format(Instant.ofEpochSecond(epochSec))

private fun formatMillis(epochMs: Long): String = DATE_TIME_FORMAT.format(Instant.ofEpochMilli(epochMs))
