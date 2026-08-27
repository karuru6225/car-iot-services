package info.karuru.cariot_mobile

import android.companion.AssociationInfo
import android.companion.CompanionDeviceService
import android.content.Intent

// CompanionDeviceManager.startObservingDevicePresence()経由でシステムからバインドされるService。
// ESP32のBLEアドバタイズを検知すると、アプリがkilled状態でもonDeviceAppearedが呼ばれる。
class CarIotCompanionService : CompanionDeviceService() {
  // API33+ (Android 13+) で呼ばれるシグネチャ
  override fun onDeviceAppeared(associationInfo: AssociationInfo) = launchApp()

  // API31/32 (Android 12/12L) で呼ばれるシグネチャ。両方overrideしないとこの範囲の実機で呼ばれない。
  override fun onDeviceAppeared(address: String) = launchApp()

  override fun onDeviceDisappeared(associationInfo: AssociationInfo) {}

  override fun onDeviceDisappeared(address: String) {}

  private fun launchApp() {
    val intent =
        Intent(this, MainActivity::class.java).apply {
          flags = Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_REORDER_TO_FRONT
          putExtra(MainActivity.EXTRA_AUTO_CONNECT, true)
        }
    startActivity(intent)
  }
}
