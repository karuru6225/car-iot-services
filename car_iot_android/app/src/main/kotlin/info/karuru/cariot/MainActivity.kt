package info.karuru.cariot

import android.Manifest
import android.os.Build
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Button
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import info.karuru.cariot.ble.BleConnectionManager

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
      var status by remember { mutableStateOf("未接続（Logcatを見てください）") }
      MaterialTheme {
        Surface {
          Column(modifier = Modifier.padding(24.dp)) {
            Text(status)
            Button(onClick = {
              status = "接続試行中..."
              requestPermissions.launch(blePermissions())
            }) {
              Text("接続")
            }
          }
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
