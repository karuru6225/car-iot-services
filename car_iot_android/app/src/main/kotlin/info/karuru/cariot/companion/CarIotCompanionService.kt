package info.karuru.cariot.companion

import android.companion.CompanionDeviceService
import android.companion.DevicePresenceEvent
import android.content.Intent
import android.util.Log
import androidx.core.content.ContextCompat
import info.karuru.cariot.service.ACTION_CONNECT
import info.karuru.cariot.service.CarIotForegroundService

private const val TAG = "CarIotCompanionService"

// CompanionDeviceManager.startObservingDevicePresence(ObservingDevicePresenceRequest)経由で
// システムからバインドされるService。ESP32のBLEアドバタイズを検知すると、アプリがkilled状態
// でもonDevicePresenceEvent()が呼ばれる（新API、docs/car_iot_android_plan.md Phase7。
// 旧API onDeviceAppeared() はAndroid16で非推奨のため使わない）。
// 画面は前面に出さず、裏でCarIotForegroundServiceのみ起動する（運転中に画面が勝手に
// 前面表示されるのを避けるため）。
class CarIotCompanionService : CompanionDeviceService() {
  override fun onCreate() {
    super.onCreate()
    Log.i(TAG, "onCreate: Serviceインスタンス生成")
  }

  override fun onDevicePresenceEvent(event: DevicePresenceEvent) {
    Log.i(TAG, "onDevicePresenceEvent: event=${event.event} associationId=${event.associationId}")
    if (event.event == DevicePresenceEvent.EVENT_BLE_APPEARED) {
      Log.i(TAG, "EVENT_BLE_APPEARED検知、CarIotForegroundServiceを起動")
      val intent = Intent(this, CarIotForegroundService::class.java).setAction(ACTION_CONNECT)
      ContextCompat.startForegroundService(this, intent)
    }
  }

  // 新APIに一本化する方針だが(docs/car_iot_android_plan.md Phase7)、実機でonDevicePresenceEvent()が
  // 呼ばれない事象の切り分けのため、システムが旧経路でコールバックしていないかも確認する。
  @Suppress("DEPRECATION")
  override fun onDeviceAppeared(address: String) {
    Log.i(TAG, "onDeviceAppeared(String): address=$address")
  }
}
