package info.karuru.cariot.companion

import android.companion.CompanionDeviceService
import android.companion.DevicePresenceEvent
import android.content.Intent
import androidx.core.content.ContextCompat
import info.karuru.cariot.service.ACTION_CONNECT
import info.karuru.cariot.service.CarIotForegroundService

// CompanionDeviceManager.startObservingDevicePresence(ObservingDevicePresenceRequest)経由で
// システムからバインドされるService。ESP32のBLEアドバタイズを検知すると、アプリがkilled状態
// でもonDevicePresenceEvent()が呼ばれる（新API、docs/car_iot_android_plan.md Phase7。
// 旧API onDeviceAppeared() はAndroid16で非推奨のため使わない）。
// 画面は前面に出さず、裏でCarIotForegroundServiceのみ起動する（運転中に画面が勝手に
// 前面表示されるのを避けるため）。
class CarIotCompanionService : CompanionDeviceService() {
  override fun onDevicePresenceEvent(event: DevicePresenceEvent) {
    if (event.event == DevicePresenceEvent.EVENT_BLE_APPEARED) {
      val intent = Intent(this, CarIotForegroundService::class.java).setAction(ACTION_CONNECT)
      ContextCompat.startForegroundService(this, intent)
    }
  }
}
