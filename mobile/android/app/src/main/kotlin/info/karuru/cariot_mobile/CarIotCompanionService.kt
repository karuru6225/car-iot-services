package info.karuru.cariot_mobile

import android.companion.AssociationInfo
import android.companion.CompanionDeviceService

// CompanionDeviceManager.startObservingDevicePresence()経由でシステムからバインドされるService。
// ESP32のBLEアドバタイズを検知すると、アプリがkilled状態でもonDeviceAppearedが呼ばれる。
class CarIotCompanionService : CompanionDeviceService() {
  // API33+ (Android 13+) で呼ばれるシグネチャ
  override fun onDeviceAppeared(associationInfo: AssociationInfo) {}

  // API31/32 (Android 12/12L) で呼ばれるシグネチャ。両方overrideしないとこの範囲の実機で呼ばれない。
  override fun onDeviceAppeared(address: String) {}

  override fun onDeviceDisappeared(associationInfo: AssociationInfo) {}

  override fun onDeviceDisappeared(address: String) {}
}
