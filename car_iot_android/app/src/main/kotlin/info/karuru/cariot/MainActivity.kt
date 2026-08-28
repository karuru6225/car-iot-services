package info.karuru.cariot

import android.Manifest
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.BatteryFull
import androidx.compose.material.icons.filled.Bluetooth
import androidx.compose.material.icons.filled.DashboardCustomize
import androidx.compose.material.icons.filled.Speed
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.NavigationBar
import androidx.compose.material3.NavigationBarItem
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.foundation.layout.padding
import androidx.compose.ui.Modifier
import androidx.core.content.ContextCompat
import androidx.lifecycle.lifecycleScope
import info.karuru.cariot.auth.AuthLoginFlow
import info.karuru.cariot.auth.AuthStore
import info.karuru.cariot.companion.CompanionDeviceHelper
import info.karuru.cariot.service.ACTION_CONNECT
import info.karuru.cariot.service.ACTION_DISCONNECT
import info.karuru.cariot.service.CarIotForegroundService
import info.karuru.cariot.state.CarIotState
import info.karuru.cariot.ui.battery.BatteryScreen
import info.karuru.cariot.ui.connection.ConnectionScreen
import info.karuru.cariot.ui.meter.MeterScreen
import info.karuru.cariot.ui.obd.ObdScreen
import info.karuru.cariot.ui.theme.AppTheme
import info.karuru.cariot.ui.theme.MinimalColorScheme
import info.karuru.cariot.ui.theme.RacingColorScheme
import info.karuru.cariot.ui.theme.ThemeStore
import kotlinx.coroutines.launch

// BLE接続・OBD受信の実処理はCarIotForegroundServiceが担当し、ここは状態(CarIotState)の
// 表示とコマンド送信だけを行う薄い層（docs/car_iot_android_plan.md Phase 3）。
// Activityが破棄されてもServiceは動き続けるため、onDestroy()でdisconnect()は呼ばない。
// 認証（Phase 4）: サインイン自体はブラウザ(Custom Tabs)を開くためActivity起点で行うが、
// 結果の受け取り・状態更新はOAuthRedirectActivity→CarIotState.userEmail経由で行う。
class MainActivity : ComponentActivity() {
  private val authLoginFlow = AuthLoginFlow(this)
  private lateinit var authStore: AuthStore
  // registerForActivityResult()をコンストラクタで呼ぶ都合上、Activity生成時（フィールド初期化時）
  // にインスタンス化する必要がある（CompanionDeviceHelper.kt参照）。
  private val companionHelper = CompanionDeviceHelper(this)
  private var companionAssociated by mutableStateOf(false)
  private var backgroundLocationGranted by mutableStateOf(false)
  private lateinit var themeStore: ThemeStore
  private var selectedTheme by mutableStateOf(AppTheme.RACING)

  private val requestPermissions =
      registerForActivityResult(ActivityResultContracts.RequestMultiplePermissions()) { granted ->
        if (granted.values.all { it }) {
          startBleService(ACTION_CONNECT)
        }
      }

  private val requestBackgroundLocation =
      registerForActivityResult(ActivityResultContracts.RequestPermission()) { granted ->
        backgroundLocationGranted = granted
      }

  override fun onCreate(savedInstanceState: Bundle?) {
    super.onCreate(savedInstanceState)
    authStore = AuthStore(applicationContext)
    companionAssociated = companionHelper.isAssociated()
    backgroundLocationGranted = hasBackgroundLocationPermission()
    themeStore = ThemeStore(applicationContext)
    selectedTheme = themeStore.load()

    // 起動時のセッション復元（mobile/lib/services/auth_service.dartのtryRestoreSession()相当、
    // 実際のトークンリフレッシュはアップロード時に行うのでここでは保存済みemailを表示するだけ）
    if (authStore.hasRefreshToken()) {
      CarIotState.setUserEmail(authStore.getEmail())
    }

    setContent {
      val colorScheme = when (selectedTheme) {
        AppTheme.RACING -> RacingColorScheme
        AppTheme.MINIMAL -> MinimalColorScheme
      }
      MaterialTheme(colorScheme = colorScheme) {
        Surface {
          var tabIndex by remember { mutableIntStateOf(0) }
          Scaffold(
              bottomBar = {
                NavigationBar {
                  NavigationBarItem(
                      selected = tabIndex == 0,
                      onClick = { tabIndex = 0 },
                      icon = { Icon(Icons.Filled.Bluetooth, contentDescription = null) },
                      label = { Text("接続") },
                  )
                  NavigationBarItem(
                      selected = tabIndex == 1,
                      onClick = { tabIndex = 1 },
                      icon = { Icon(Icons.Filled.BatteryFull, contentDescription = null) },
                      label = { Text("バッテリー") },
                  )
                  NavigationBarItem(
                      selected = tabIndex == 2,
                      onClick = { tabIndex = 2 },
                      icon = { Icon(Icons.Filled.Speed, contentDescription = null) },
                      label = { Text("OBD") },
                  )
                  NavigationBarItem(
                      selected = tabIndex == 3,
                      onClick = { tabIndex = 3 },
                      icon = { Icon(Icons.Filled.DashboardCustomize, contentDescription = null) },
                      label = { Text("メーター") },
                  )
                }
              },
          ) { innerPadding ->
            Surface(modifier = Modifier.padding(innerPadding)) {
              when (tabIndex) {
                0 -> ConnectionScreen(
                    onConnect = { requestPermissions.launch(blePermissions()) },
                    onDisconnect = { startBleService(ACTION_DISCONNECT) },
                    onSignIn = { signIn() },
                    onSignOut = { signOut() },
                    companionAssociated = companionAssociated,
                    onEnableAutoLaunch = { enableAutoLaunch() },
                    backgroundLocationGranted = backgroundLocationGranted,
                    onRequestBackgroundLocation = {
                      requestBackgroundLocation.launch(Manifest.permission.ACCESS_BACKGROUND_LOCATION)
                    },
                    selectedTheme = selectedTheme,
                    onThemeChange = { theme ->
                      selectedTheme = theme
                      themeStore.save(theme)
                    },
                )
                1 -> BatteryScreen()
                2 -> ObdScreen()
                else -> MeterScreen()
              }
            }
          }
        }
      }
    }
  }

  override fun onResume() {
    super.onResume()
    // システム設定アプリ経由で許可された場合(Android 11+の一般的なフロー)は
    // ActivityResultのコールバックを通らないため、画面復帰のたびに再チェックする。
    backgroundLocationGranted = hasBackgroundLocationPermission()
  }

  private fun hasBackgroundLocationPermission(): Boolean {
    return ContextCompat.checkSelfPermission(
        this,
        Manifest.permission.ACCESS_BACKGROUND_LOCATION,
    ) == PackageManager.PERMISSION_GRANTED
  }

  // ESP32とのCDM関連付けを行い、アプリがkilled状態でもBLE検知でCarIotForegroundServiceのみ
  // 自動起動できるようにする（画面は前面に出さない、CarIotCompanionService.kt参照、Phase7）。
  private fun enableAutoLaunch() {
    lifecycleScope.launch {
      if (companionHelper.associate()) {
        companionAssociated = true
      }
    }
  }

  private fun signIn() {
    lifecycleScope.launch {
      authLoginFlow.startSignIn()
    }
  }

  private fun signOut() {
    authStore.clear()
    CarIotState.setUserEmail(null)
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
    }
    // BLEスキャン自体はneverForLocationフラグにより位置情報権限を必要としないが、
    // OBDデータへのGPS位置紐付け(Phase6、LocationTracker)のため別途リクエストする。
    perms.add(Manifest.permission.ACCESS_FINE_LOCATION)
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
      perms.add(Manifest.permission.POST_NOTIFICATIONS)
    }
    return perms.toTypedArray()
  }
}
