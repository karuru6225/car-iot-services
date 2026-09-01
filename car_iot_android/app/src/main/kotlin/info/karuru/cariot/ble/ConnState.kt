package info.karuru.cariot.ble

// mobile/lib/models/conn_state.dartの移植。
enum class ConnState {
  DISCONNECTED,
  SCANNING,
  CONNECTING,
  CONNECTED,
}

data class Measurement(
    val vMain: Float? = null,
    val curr: Float? = null,
    val pwr: Float? = null,
    val vSub: Float? = null,
)
