package info.karuru.cariot_mobile

import android.bluetooth.le.ScanFilter
import android.companion.AssociationInfo
import android.companion.AssociationRequest
import android.companion.BluetoothLeDeviceFilter
import android.companion.CompanionDeviceManager
import android.content.Context
import android.content.Intent
import android.content.IntentSender
import android.content.SharedPreferences
import android.os.Build
import android.os.Bundle
import android.os.ParcelUuid
import io.flutter.embedding.android.FlutterActivity
import io.flutter.embedding.engine.FlutterEngine
import io.flutter.plugin.common.MethodChannel
import java.util.regex.Pattern

private const val CHANNEL = "info.karuru.cariot_mobile/companion"
private const val PREFS_NAME = "companion_prefs"
private const val PREF_DEVICE_ADDRESS = "device_address"
private const val REQUEST_CODE_ASSOCIATE = 42

class MainActivity : FlutterActivity() {
  private lateinit var prefs: SharedPreferences
  private var channel: MethodChannel? = null

  override fun onCreate(savedInstanceState: Bundle?) {
    super.onCreate(savedInstanceState)
    prefs = getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
  }

  override fun configureFlutterEngine(flutterEngine: FlutterEngine) {
    super.configureFlutterEngine(flutterEngine)
    channel = MethodChannel(flutterEngine.dartExecutor.binaryMessenger, CHANNEL)
    channel!!.setMethodCallHandler { call, result ->
      when (call.method) {
        "associate" -> {
          val serviceUuid = call.argument<String>("serviceUuid")!!
          val namePrefix = call.argument<String>("namePrefix") ?: "car-iot-"
          associate(serviceUuid, namePrefix)
          result.success(null)
        }
        "isAssociated" -> result.success(prefs.contains(PREF_DEVICE_ADDRESS))
        else -> result.notImplemented()
      }
    }
  }

  private fun associate(serviceUuid: String, namePrefix: String) {
    val manager = getSystemService(Context.COMPANION_DEVICE_SERVICE) as CompanionDeviceManager
    val scanFilter = ScanFilter.Builder().setServiceUuid(ParcelUuid.fromString(serviceUuid)).build()
    val filter =
        BluetoothLeDeviceFilter.Builder()
            .setScanFilter(scanFilter)
            .setNamePattern(Pattern.compile("$namePrefix.*"))
            .build()
    val request = AssociationRequest.Builder().addDeviceFilter(filter).setSingleDevice(true).build()
    manager.associate(
        request,
        object : CompanionDeviceManager.Callback() {
          override fun onAssociationPending(intentSender: IntentSender) {
            // FlutterActivityはandroidx.activity.ComponentActivityではなくandroid.app.Activityを
            // 直接継承しているため、registerForActivityResult()は使えない。従来のstartIntentSenderForResult
            // + onActivityResultで結果を受け取る。
            startIntentSenderForResult(intentSender, REQUEST_CODE_ASSOCIATE, null, 0, 0, 0)
          }

          override fun onAssociationCreated(associationInfo: AssociationInfo) {
            onAssociated(associationInfo.deviceMacAddress?.toString())
          }

          override fun onFailure(error: CharSequence?) {
            // Dart側は関連付けボタン押下後の状態確認(isAssociated)で失敗を判断する
          }
        },
        null,
    )
  }

  override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
    if (requestCode != REQUEST_CODE_ASSOCIATE) {
      super.onActivityResult(requestCode, resultCode, data)
      return
    }
    val address =
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
          data
              ?.getParcelableExtra(CompanionDeviceManager.EXTRA_ASSOCIATION, AssociationInfo::class.java)
              ?.deviceMacAddress
              ?.toString()
        } else {
          @Suppress("DEPRECATION")
          data?.getParcelableExtra<android.bluetooth.BluetoothDevice>(CompanionDeviceManager.EXTRA_DEVICE)
              ?.address
        }
    if (address != null) onAssociated(address)
  }

  private fun onAssociated(address: String?) {
    if (address == null) return
    prefs.edit().putString(PREF_DEVICE_ADDRESS, address).apply()
    val manager = getSystemService(Context.COMPANION_DEVICE_SERVICE) as CompanionDeviceManager
    manager.startObservingDevicePresence(address)
  }
}
