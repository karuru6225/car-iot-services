package info.karuru.cariot

import android.Manifest
import android.content.Intent
import android.os.Build
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Button
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import androidx.core.content.ContextCompat
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import info.karuru.cariot.ble.ConnState
import info.karuru.cariot.service.ACTION_CONNECT
import info.karuru.cariot.service.ACTION_DISCONNECT
import info.karuru.cariot.service.CarIotForegroundService
import info.karuru.cariot.state.CarIotState

// BLE接続・OBD受信の実処理はCarIotForegroundServiceが担当し、ここは状態(CarIotState)の
// 表示とコマンド送信だけを行う薄い層（docs/car_iot_android_plan.md Phase 3）。
// Activityが破棄されてもServiceは動き続けるため、onDestroy()でdisconnect()は呼ばない。
class MainActivity : ComponentActivity() {
  private val requestPermissions =
      registerForActivityResult(ActivityResultContracts.RequestMultiplePermissions()) { granted ->
        if (granted.values.all { it }) {
          startBleService(ACTION_CONNECT)
        }
      }

  override fun onCreate(savedInstanceState: Bundle?) {
    super.onCreate(savedInstanceState)

    setContent {
      MaterialTheme {
        Surface {
          ConnectionScreen(
              onConnect = { requestPermissions.launch(blePermissions()) },
              onDisconnect = { startBleService(ACTION_DISCONNECT) },
          )
        }
      }
    }
  }

  private fun startBleService(action: String) {
    val intent = Intent(this, CarIotForegroundService::class.java).setAction(action)
    ContextCompat.startForegroundService(this, intent)
  }

  private fun blePermissions(): Array<String> {
    val perms = mutableListOf<String>()
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
      perms.add(Manifest.permission.BLUETOOTH_SCAN)
      perms.add(Manifest.permission.BLUETOOTH_CONNECT)
    } else {
      perms.add(Manifest.permission.ACCESS_FINE_LOCATION)
    }
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
      perms.add(Manifest.permission.POST_NOTIFICATIONS)
    }
    return perms.toTypedArray()
  }
}

@Composable
private fun ConnectionScreen(onConnect: () -> Unit, onDisconnect: () -> Unit) {
  val state by CarIotState.connState.collectAsStateWithLifecycle()
  val deviceName by CarIotState.deviceName.collectAsStateWithLifecycle()
  val measurement by CarIotState.measurement.collectAsStateWithLifecycle()
  val obdReading by CarIotState.obdReading.collectAsStateWithLifecycle()

  val label = when (state) {
    ConnState.DISCONNECTED -> "未接続"
    ConnState.SCANNING -> "スキャン中..."
    ConnState.CONNECTING -> "接続中..."
    ConnState.CONNECTED -> "接続済み: $deviceName"
  }
  val isConnected = state == ConnState.CONNECTED
  val isBusy = state == ConnState.SCANNING || state == ConnState.CONNECTING

  Column(modifier = Modifier.padding(24.dp)) {
    Text(label)
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
    Text("vMain=${measurement.vMain ?: "—"}", modifier = Modifier.padding(top = 16.dp))
    Text("curr=${measurement.curr ?: "—"}")
    Text("pwr=${measurement.pwr ?: "—"}")
    Text("vSub=${measurement.vSub ?: "—"}")
    Text("OBD: ${obdReading?.let { if (it.valid) "rpm=${it.rpm} speed=${it.speedKmh}" else "応答なし" } ?: "—"}",
        modifier = Modifier.padding(top = 16.dp))
  }
}
