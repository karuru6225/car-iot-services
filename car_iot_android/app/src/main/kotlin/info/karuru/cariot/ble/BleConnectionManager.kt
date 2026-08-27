package info.karuru.cariot.ble

import android.annotation.SuppressLint
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCallback
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothGattDescriptor
import android.bluetooth.BluetoothManager
import android.bluetooth.BluetoothProfile
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanFilter
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.Context
import android.os.Handler
import android.os.Looper
import android.os.ParcelUuid
import android.util.Log
import info.karuru.cariot.obd.ObdReading
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.UUID
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

private const val TAG = "BleConnectionManager"
private val CCCD_UUID = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")
private const val SCAN_TIMEOUT_MS = 10_000L

// mobile/lib/screens/ble_home_screen.dartの_connect()/_tryConnectOnce()/_discoverAndSubscribe()
// のKotlin移植。スキャン→接続→サービスディスカバリ→計測値4種+OBDのNotify購読→切断検知で
// 自動再接続、をCONNECT_RETRY_WINDOW_MSの間リトライし続ける。
// 状態(接続状態・計測値・OBD値)はStateFlowで公開し、UI(MainActivity)はそれを購読するだけにする。
@SuppressLint("MissingPermission") // 呼び出し側(MainActivity)で実行時権限を確認済み前提
class BleConnectionManager(private val context: Context) {
  private val bluetoothManager = context.getSystemService(BluetoothManager::class.java)
  private val adapter = bluetoothManager.adapter
  private val handler = Handler(Looper.getMainLooper())
  private val obdAssembler = ObdChunkAssembler()

  private val _connState = MutableStateFlow(ConnState.DISCONNECTED)
  val connState: StateFlow<ConnState> = _connState.asStateFlow()

  private val _deviceName = MutableStateFlow("")
  val deviceName: StateFlow<String> = _deviceName.asStateFlow()

  private val _measurement = MutableStateFlow(Measurement())
  val measurement: StateFlow<Measurement> = _measurement.asStateFlow()

  private val _obdReading = MutableStateFlow<ObdReading?>(null)
  val obdReading: StateFlow<ObdReading?> = _obdReading.asStateFlow()

  private var gatt: BluetoothGatt? = null
  private var userDisconnected = false
  private var retryDeadline: Long = 0
  private var deviceFound = false

  fun connect() {
    // 二重接続防止: 既に接続中/接続試行中なら何もしない
    if (_connState.value != ConnState.DISCONNECTED) return
    userDisconnected = false
    retryDeadline = System.currentTimeMillis() + CONNECT_RETRY_WINDOW_MS
    startScan()
  }

  fun disconnect() {
    userDisconnected = true
    adapter.bluetoothLeScanner?.stopScan(scanCallback)
    gatt?.disconnect()
    cleanup()
  }

  private fun startScan() {
    if (System.currentTimeMillis() > retryDeadline) {
      Log.i(TAG, "接続リトライを終了しました（${CONNECT_RETRY_WINDOW_MS / 60_000}分経過）")
      cleanup()
      return
    }
    Log.i(TAG, "スキャン開始...")
    _connState.value = ConnState.SCANNING
    deviceFound = false
    val filter = ScanFilter.Builder().setServiceUuid(ParcelUuid(MEAS_SERVICE_UUID)).build()
    val settings = ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY).build()
    adapter.bluetoothLeScanner?.startScan(listOf(filter), settings, scanCallback)
    handler.postDelayed({
      adapter.bluetoothLeScanner?.stopScan(scanCallback)
      // タイムアウトまでにデバイスが見つからなかった場合、ここでリトライをスケジュールしないと
      // スキャンが1回で終わってしまう（Flutter版の_tryConnectOnce()のtimeout→リトライに相当）。
      if (!deviceFound && !userDisconnected) {
        Log.i(TAG, "デバイスが見つかりません (${SCAN_TIMEOUT_MS / 1000}s)")
        scheduleRetry()
      }
    }, SCAN_TIMEOUT_MS)
  }

  private val scanCallback = object : ScanCallback() {
    override fun onScanResult(callbackType: Int, result: ScanResult) {
      deviceFound = true
      adapter.bluetoothLeScanner?.stopScan(this)
      Log.i(TAG, "発見: ${result.device.name}")
      connectToDevice(result.device)
    }

    override fun onScanFailed(errorCode: Int) {
      Log.w(TAG, "スキャン失敗: errorCode=$errorCode")
      scheduleRetry()
    }
  }

  private fun connectToDevice(device: BluetoothDevice) {
    Log.i(TAG, "接続中: ${device.name}")
    _connState.value = ConnState.CONNECTING
    _deviceName.value = device.name ?: ""
    gatt = device.connectGatt(context, false, gattCallback)
  }

  private fun scheduleRetry() {
    if (userDisconnected) return
    handler.postDelayed({ startScan() }, CONNECT_RETRY_DELAY_MS)
  }

  private val gattCallback = object : BluetoothGattCallback() {
    override fun onConnectionStateChange(g: BluetoothGatt, status: Int, newState: Int) {
      if (newState == BluetoothProfile.STATE_CONNECTED) {
        Log.i(TAG, "接続完了、サービスディスカバリ開始")
        g.discoverServices()
      } else if (newState == BluetoothProfile.STATE_DISCONNECTED) {
        val autoReconnect = !userDisconnected
        Log.i(TAG, if (autoReconnect) "予期しない切断: 自動再接続..." else "切断されました")
        g.close()
        gatt = null
        cleanup()
        if (autoReconnect) {
          retryDeadline = System.currentTimeMillis() + CONNECT_RETRY_WINDOW_MS
          handler.postDelayed({ startScan() }, 3_000)
        }
      }
    }

    override fun onServicesDiscovered(g: BluetoothGatt, status: Int) {
      val service = g.getService(MEAS_SERVICE_UUID)
      if (service == null) {
        Log.w(TAG, "計測サービスが見つかりません")
        return
      }
      for (uuid in listOf(VOLT_MAIN_CHAR_UUID, CURR_CHAR_UUID, PWR_CHAR_UUID, VOLT_SUB_CHAR_UUID, OBD_CHAR_UUID)) {
        val characteristic = service.getCharacteristic(uuid) ?: continue
        enableNotify(g, characteristic)
      }
      Log.i(TAG, "Notify購読開始")
      _connState.value = ConnState.CONNECTED
    }

    override fun onCharacteristicChanged(
        g: BluetoothGatt,
        characteristic: BluetoothGattCharacteristic,
        value: ByteArray,
    ) {
      when (characteristic.uuid) {
        VOLT_MAIN_CHAR_UUID -> _measurement.value = _measurement.value.copy(vMain = parseFloat32(value))
        CURR_CHAR_UUID -> _measurement.value = _measurement.value.copy(curr = parseFloat32(value))
        PWR_CHAR_UUID -> _measurement.value = _measurement.value.copy(pwr = parseFloat32(value))
        VOLT_SUB_CHAR_UUID -> _measurement.value = _measurement.value.copy(vSub = parseFloat32(value))
        OBD_CHAR_UUID -> onObdChunk(value)
      }
    }
  }

  private fun enableNotify(g: BluetoothGatt, characteristic: BluetoothGattCharacteristic) {
    g.setCharacteristicNotification(characteristic, true)
    val descriptor = characteristic.getDescriptor(CCCD_UUID) ?: return
    @Suppress("DEPRECATION")
    descriptor.value = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
    @Suppress("DEPRECATION")
    g.writeDescriptor(descriptor)
  }

  private fun onObdChunk(raw: ByteArray) {
    try {
      val combined = obdAssembler.add(raw) ?: return
      _obdReading.value = ObdReading.fromBytes(combined)
    } catch (e: Exception) {
      Log.e(TAG, "OBDデータ解析エラー", e)
    }
  }

  private fun parseFloat32(value: ByteArray): Float {
    return ByteBuffer.wrap(value).order(ByteOrder.LITTLE_ENDIAN).float
  }

  private fun cleanup() {
    obdAssembler.reset()
    _connState.value = ConnState.DISCONNECTED
    _deviceName.value = ""
    _measurement.value = Measurement()
    _obdReading.value = null
  }
}
