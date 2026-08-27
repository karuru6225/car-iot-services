package info.karuru.cariot

import android.Manifest
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
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import info.karuru.cariot.ble.BleConnectionManager
import info.karuru.cariot.ble.ConnState

class MainActivity : ComponentActivity() {
  private lateinit var bleManager: BleConnectionManager

  private val requestPermissions =
      registerForActivityResult(ActivityResultContracts.RequestMultiplePermissions()) { granted ->
        if (granted.values.all { it }) {
          bleManager.connect()
        }
      }

  override fun onCreate(savedInstanceState: Bundle?) {
    super.onCreate(savedInstanceState)
    bleManager = BleConnectionManager(applicationContext)

    setContent {
      MaterialTheme {
        Surface {
          ConnectionScreen(
              bleManager = bleManager,
              onConnect = { requestPermissions.launch(blePermissions()) },
          )
        }
      }
    }
  }

  override fun onDestroy() {
    bleManager.disconnect()
    super.onDestroy()
  }

  private fun blePermissions(): Array<String> {
    return if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
      arrayOf(Manifest.permission.BLUETOOTH_SCAN, Manifest.permission.BLUETOOTH_CONNECT)
    } else {
      arrayOf(Manifest.permission.ACCESS_FINE_LOCATION)
    }
  }
}

@Composable
private fun ConnectionScreen(bleManager: BleConnectionManager, onConnect: () -> Unit) {
  val state by bleManager.connState.collectAsStateWithLifecycle()
  val deviceName by bleManager.deviceName.collectAsStateWithLifecycle()
  val measurement by bleManager.measurement.collectAsStateWithLifecycle()
  val obdReading by bleManager.obdReading.collectAsStateWithLifecycle()

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
          onClick = { bleManager.disconnect() },
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
