package info.karuru.cariot.ui.connection

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Button
import androidx.compose.material3.FilterChip
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import info.karuru.cariot.ble.ConnState
import info.karuru.cariot.state.CarIotState
import info.karuru.cariot.ui.theme.AppTheme

// 「接続」タブ。サインイン/アウト・BLE接続状態・自動起動(CDM)・バックグラウンド位置情報の
// 許可状態・テーマ切り替えを表示する。計測値・OBD値の表示はbattery/obdタブに分離した（Phase8）。
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
) {
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

  Column(modifier = Modifier.padding(24.dp)) {
    Text(userEmail?.let { "ログイン: $it" } ?: "未ログイン")
    Row(modifier = Modifier.padding(top = 8.dp)) {
      if (userEmail == null) {
        Button(onClick = onSignIn) { Text("サインイン") }
      } else {
        Button(onClick = onSignOut) { Text("サインアウト") }
      }
    }

    Text(label, modifier = Modifier.padding(top = 24.dp))
    Row(modifier = Modifier.padding(top = 12.dp)) {
      Button(onClick = onConnect, enabled = !isConnected && !isBusy) {
        Text("接続")
      }
      Button(
          onClick = onDisconnect,
          enabled = isConnected || isBusy,
          modifier = Modifier.padding(start = 12.dp),
      ) {
        Text(if (isBusy) "中止" else "切断")
      }
    }

    Row(modifier = Modifier.padding(top = 24.dp)) {
      if (companionAssociated) {
        Text("自動起動: 有効")
      } else {
        Button(onClick = onEnableAutoLaunch) { Text("自動起動を有効にする") }
      }
    }
    // 自動起動時に起動するCarIotForegroundServiceはlocation型FGSのため、
    // ACCESS_BACKGROUND_LOCATIONが無いと起動時にクラッシュする（実機で確認済み、Phase7）。
    Row(modifier = Modifier.padding(top = 12.dp)) {
      if (backgroundLocationGranted) {
        Text("バックグラウンド位置情報: 許可済み")
      } else {
        Button(onClick = onRequestBackgroundLocation) { Text("バックグラウンド位置情報を許可する") }
      }
    }

    Text("テーマ", modifier = Modifier.padding(top = 24.dp))
    Row(modifier = Modifier.padding(top = 8.dp)) {
      AppTheme.entries.forEach { theme ->
        FilterChip(
            selected = selectedTheme == theme,
            onClick = { onThemeChange(theme) },
            label = { Text(theme.label) },
            modifier = Modifier.padding(end = 8.dp),
        )
      }
    }
  }
}
