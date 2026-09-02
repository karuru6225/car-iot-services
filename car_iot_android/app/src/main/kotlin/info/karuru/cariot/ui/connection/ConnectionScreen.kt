package info.karuru.cariot.ui.connection

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.FilterChip
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedCard
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import info.karuru.cariot.ble.ConnState
import info.karuru.cariot.meter.MeterSlot
import info.karuru.cariot.state.CarIotState
import info.karuru.cariot.ui.meter.MeterSettingsSheet
import info.karuru.cariot.ui.theme.AppTheme
import info.karuru.cariot.ui.theme.ClusterButtonShape

// 「接続」タブ。サインイン/アウト・BLE接続状態・自動起動(CDM)・バックグラウンド位置情報の
// 許可状態・テーマ切り替えを表示する。計測値・OBD値の表示はbattery/obdタブに分離した（Phase8）。
//
// デザイン(2026/08): 「接続」だけを塗りつぶしボタンにし、他は全てOutlinedButtonにして
// プライマリアクションの視覚的階層を作る。セクションはOutlinedCardで区切り、見出しは
// 字間を開けた極小テキストで計器盤のパネル区画名に似せている。
@OptIn(ExperimentalLayoutApi::class)
@Composable
fun ConnectionScreen(
    onConnect: () -> Unit,
    onDisconnect: () -> Unit,
    onSignIn: () -> Unit,
    onSignOut: () -> Unit,
    companionAssociated: Boolean,
    onEnableAutoLaunch: () -> Unit,
    backgroundLocationGranted: Boolean,
    onRequestBackgroundLocation: () -> Unit,
    selectedTheme: AppTheme,
    onThemeChange: (AppTheme) -> Unit,
    pipSlots: List<MeterSlot>,
    onPipSlotsChange: (List<MeterSlot>) -> Unit,
) {
  var showPipDialog by remember { mutableStateOf(false) }
  val state by CarIotState.connState.collectAsStateWithLifecycle()
  val deviceName by CarIotState.deviceName.collectAsStateWithLifecycle()
  val userEmail by CarIotState.userEmail.collectAsStateWithLifecycle()

  val label = when (state) {
    ConnState.DISCONNECTED -> "未接続"
    ConnState.SCANNING -> "スキャン中..."
    ConnState.CONNECTING -> "接続中..."
    ConnState.CONNECTED -> "接続済み: $deviceName"
  }
  val isConnected = state == ConnState.CONNECTED
  val isBusy = state == ConnState.SCANNING || state == ConnState.CONNECTING

  Column(
      modifier = Modifier
          .fillMaxWidth()
          .verticalScroll(rememberScrollState())
          .padding(20.dp),
  ) {
    SectionCard(title = "アカウント") {
      Text(userEmail?.let { "ログイン: $it" } ?: "未ログイン")
      Spacer(Modifier.height(12.dp))
      if (userEmail == null) {
        OutlinedButton(onClick = onSignIn, shape = ClusterButtonShape) { Text("サインイン") }
      } else {
        OutlinedButton(onClick = onSignOut, shape = ClusterButtonShape) { Text("サインアウト") }
      }
    }

    SectionCard(title = "BLE接続") {
      Text(label)
      Row(modifier = Modifier.padding(top = 12.dp)) {
        Button(onClick = onConnect, enabled = !isConnected && !isBusy, shape = ClusterButtonShape) {
          Text("接続")
        }
        OutlinedButton(
            onClick = onDisconnect,
            enabled = isConnected || isBusy,
            shape = ClusterButtonShape,
            modifier = Modifier.padding(start = 12.dp),
        ) {
          Text(if (isBusy) "中止" else "切断")
        }
      }

      Spacer(Modifier.height(16.dp))
      if (companionAssociated) {
        Text("自動起動: 有効")
      } else {
        OutlinedButton(onClick = onEnableAutoLaunch, shape = ClusterButtonShape) {
          Text("自動起動を有効にする")
        }
      }

      // 自動起動時に起動するCarIotForegroundServiceはlocation型FGSのため、
      // ACCESS_BACKGROUND_LOCATIONが無いと起動時にクラッシュする（実機で確認済み、Phase7）。
      Spacer(Modifier.height(12.dp))
      if (backgroundLocationGranted) {
        Text("バックグラウンド位置情報: 許可済み")
      } else {
        OutlinedButton(onClick = onRequestBackgroundLocation, shape = ClusterButtonShape) {
          Text("バックグラウンド位置情報を許可する")
        }
      }
    }

    SectionCard(title = "テーマ") {
      // テーマが増えても横1列に詰め込まず折り返す（メーター設定のゲージ種別で
      // 4つ目が画面外に切れて選べなくなった件と同じ壊れ方を避ける）。
      FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
        AppTheme.entries.forEach { theme ->
          FilterChip(
              selected = selectedTheme == theme,
              onClick = { onThemeChange(theme) },
              label = { Text(theme.label) },
              shape = ClusterButtonShape,
          )
        }
      }
    }

    SectionCard(title = "ピクチャーインピクチャー") {
      OutlinedButton(onClick = { showPipDialog = true }, shape = ClusterButtonShape) {
        Text("表示項目を設定")
      }
    }
  }

  if (showPipDialog) {
    // メータータブと同じ編集シートを流用する。項目とゲージ種別の選び方が同一になるので、
    // PiP専用の操作を覚え直さなくて済む。
    MeterSettingsSheet(
        slots = pipSlots,
        onDismiss = { showPipDialog = false },
        onSave = {
          onPipSlotsChange(it)
          showPipDialog = false
        },
        title = "PiP表示項目を編集",
    )
  }
}

@Composable
private fun SectionCard(title: String, content: @Composable ColumnScope.() -> Unit) {
  OutlinedCard(
      modifier = Modifier
          .fillMaxWidth()
          .padding(vertical = 8.dp),
  ) {
    Column(modifier = Modifier.padding(16.dp)) {
      // 見出しはアクセント色ではなく前景色。小さな文字(11sp)にアクセント色を使うと
      // テーマによってWCAGの4.5:1を満たせず、実際の計器も区画名は白で刻印されていて
      // 色が乗るのは針の方であるため。
      Text(
          title,
          style = MaterialTheme.typography.titleMedium,
          color = MaterialTheme.colorScheme.onSurface,
      )
      Spacer(Modifier.height(12.dp))
      content()
    }
  }
}
