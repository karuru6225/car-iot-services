package info.karuru.cariot.companion

import android.bluetooth.le.ScanFilter
import android.companion.AssociationInfo
import android.companion.AssociationRequest
import android.companion.BluetoothLeDeviceFilter
import android.companion.CompanionDeviceManager
import android.companion.ObservingDevicePresenceRequest
import android.content.Context
import android.content.IntentSender
import android.os.ParcelUuid
import androidx.activity.ComponentActivity
import androidx.activity.result.IntentSenderRequest
import androidx.activity.result.contract.ActivityResultContracts
import info.karuru.cariot.ble.DEVICE_NAME_PREFIX
import info.karuru.cariot.ble.MEAS_SERVICE_UUID
import kotlinx.coroutines.CancellableContinuation
import kotlinx.coroutines.suspendCancellableCoroutine
import java.util.regex.Pattern

private const val PREFS_NAME = "companion_prefs"
private const val KEY_ASSOCIATION_ID = "association_id"

// mobile/android版(feat/ble-companion-auto-launchブランチ)のCDM関連付け処理のKotlin移植。
// ただし新API(startObservingDevicePresence(ObservingDevicePresenceRequest))はAPI36必須
// （旧API startObservingDevicePresence(String)/onDeviceAppeared() はAndroid16で非推奨化）
// のため、こちらに一本化した(minSdkも36へ引き上げ済み、docs/car_iot_android_plan.md Phase7)。
// ComponentActivityのregisterForActivityResult()を使うため、Activity生成時（フィールド初期化時）
// にインスタンス化する必要がある（onCreate()の中で初めて生成してはいけない）。
class CompanionDeviceHelper(private val activity: ComponentActivity) {
  // Activityのコンストラクタ実行時点ではattachBaseContext()未実行でContext系メソッドを
  // 呼ぶとNPEになるため(実機で確認済み)、実際に使われるまで初期化を遅延させる。
  private val prefs by lazy { activity.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE) }
  private var pendingContinuation: CancellableContinuation<Boolean>? = null

  private val associateLauncher = activity.registerForActivityResult(
      ActivityResultContracts.StartIntentSenderForResult(),
  ) { result ->
    val info = result.data?.getParcelableExtra(
        CompanionDeviceManager.EXTRA_ASSOCIATION,
        AssociationInfo::class.java,
    )
    if (info != null) onAssociated(info)
    pendingContinuation?.takeIf { it.isActive }?.resumeWith(Result.success(info != null))
    pendingContinuation = null
  }

  fun isAssociated(): Boolean = prefs.contains(KEY_ASSOCIATION_ID)

  // ESP32とのCDM関連付けを開始する。ユーザーがシステムダイアログでデバイスを選択完了する
  // (または キャンセルする)までサスペンドする。
  suspend fun associate(): Boolean = suspendCancellableCoroutine { cont ->
    pendingContinuation = cont
    val manager = activity.getSystemService(Context.COMPANION_DEVICE_SERVICE) as CompanionDeviceManager
    val scanFilter = ScanFilter.Builder().setServiceUuid(ParcelUuid(MEAS_SERVICE_UUID)).build()
    val filter = BluetoothLeDeviceFilter.Builder()
        .setScanFilter(scanFilter)
        .setNamePattern(Pattern.compile("$DEVICE_NAME_PREFIX.*"))
        .build()
    val request = AssociationRequest.Builder().addDeviceFilter(filter).setSingleDevice(true).build()
    manager.associate(
        request,
        object : CompanionDeviceManager.Callback() {
          override fun onAssociationPending(intentSender: IntentSender) {
            associateLauncher.launch(IntentSenderRequest.Builder(intentSender).build())
          }

          override fun onAssociationCreated(associationInfo: AssociationInfo) {
            // 通常のBLEデバイスフィルタ関連付けではonAssociationPending経由のActivity Resultで
            // 完結するため基本的に呼ばれないが、念のため保険として実装しておく。
            onAssociated(associationInfo)
            if (cont.isActive) cont.resumeWith(Result.success(true))
          }

          override fun onFailure(error: CharSequence?) {
            if (cont.isActive) cont.resumeWith(Result.success(false))
          }
        },
        null,
    )
  }

  private fun onAssociated(info: AssociationInfo) {
    prefs.edit().putInt(KEY_ASSOCIATION_ID, info.id).apply()
    val manager = activity.getSystemService(Context.COMPANION_DEVICE_SERVICE) as CompanionDeviceManager
    manager.startObservingDevicePresence(
        ObservingDevicePresenceRequest.Builder().setAssociationId(info.id).build(),
    )
  }
}
