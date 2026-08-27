package info.karuru.cariot.state

import info.karuru.cariot.ble.ConnState
import info.karuru.cariot.ble.Measurement
import info.karuru.cariot.obd.ObdReading
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

// プロセス内シングルトン。CarIotForegroundServiceが書き込み、UI(MainActivity)はStateFlowを
// 購読するだけにする（Binderバインドを使わない設計、docs/car_iot_android_plan.md参照）。
// Activityが破棄されていてもServiceが状態を保持し続け、UIは戻ってきたら最新状態を受け取る。
object CarIotState {
  private val _connState = MutableStateFlow(ConnState.DISCONNECTED)
  val connState: StateFlow<ConnState> = _connState.asStateFlow()

  private val _deviceName = MutableStateFlow("")
  val deviceName: StateFlow<String> = _deviceName.asStateFlow()

  private val _measurement = MutableStateFlow(Measurement())
  val measurement: StateFlow<Measurement> = _measurement.asStateFlow()

  private val _obdReading = MutableStateFlow<ObdReading?>(null)
  val obdReading: StateFlow<ObdReading?> = _obdReading.asStateFlow()

  // サインイン中のユーザーのメールアドレス（表示用）。未サインインならnull。
  private val _userEmail = MutableStateFlow<String?>(null)
  val userEmail: StateFlow<String?> = _userEmail.asStateFlow()

  fun setUserEmail(v: String?) {
    _userEmail.value = v
  }

  fun setConnState(v: ConnState) {
    _connState.value = v
  }

  fun setDeviceName(v: String) {
    _deviceName.value = v
  }

  fun updateMeasurement(update: (Measurement) -> Measurement) {
    _measurement.value = update(_measurement.value)
  }

  fun setObdReading(v: ObdReading?) {
    _obdReading.value = v
  }

  fun reset() {
    _connState.value = ConnState.DISCONNECTED
    _deviceName.value = ""
    _measurement.value = Measurement()
    _obdReading.value = null
  }
}
