package info.karuru.cariot.state

import info.karuru.cariot.ble.ConnState
import info.karuru.cariot.ble.Measurement
import info.karuru.cariot.obd.ObdMetric
import info.karuru.cariot.obd.ObdReading
import info.karuru.cariot.obd.obdMetricMeta
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

private const val OBD_HISTORY_CAPACITY = 60

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

  // メーター画面のミニグラフ(sparkline)用リングバッファ。Room永続化とは別物で、表示専用・
  // 古いものは捨ててよいデータのためメモリ上にのみ保持する（docs/car_iot_android_plan.md）。
  private val _obdHistory = MutableStateFlow<Map<ObdMetric, List<Float>>>(emptyMap())
  val obdHistory: StateFlow<Map<ObdMetric, List<Float>>> = _obdHistory.asStateFlow()

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

  // メーター画面のsparkline用に全項目の値を履歴へ積む（mobile/lib/screens/ble_home_screen.dartの
  // _pushHistory()を移植）。範囲外(meta.min/max外)の値は無応答フレーム同様スキップする。
  // アイドリングストップ切替瞬間等にPIDデコードが化けた値をそのまま積むと、固定レンジの
  // sparklineグラフを貫く縦線のようなスパイクになってしまうため。
  fun pushObdHistory(reading: ObdReading) {
    val updated = _obdHistory.value.toMutableMap()
    for (metric in ObdMetric.entries) {
      val meta = obdMetricMeta.getValue(metric)
      val value = meta.valueOf(reading)
      if (value < meta.min || value > meta.max) continue
      val list = updated[metric].orEmpty() + value
      updated[metric] = if (list.size > OBD_HISTORY_CAPACITY) {
        list.takeLast(OBD_HISTORY_CAPACITY)
      } else {
        list
      }
    }
    _obdHistory.value = updated
  }

  fun reset() {
    _connState.value = ConnState.DISCONNECTED
    _deviceName.value = ""
    _measurement.value = Measurement()
    _obdReading.value = null
    _obdHistory.value = emptyMap()
  }
}
