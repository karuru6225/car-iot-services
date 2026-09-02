package info.karuru.cariot.ble

import java.util.UUID

// 計測サービス（ESP32側 device/ble_peripheral.cpp の MEAS_SERVICE_UUID と一致させること）
val MEAS_SERVICE_UUID: UUID = UUID.fromString("f3a8b2c1-d4e5-4f6a-7b8c-9d0e1f2a3b4c")
val VOLT_MAIN_CHAR_UUID: UUID = UUID.fromString("f3a8b2c2-d4e5-4f6a-7b8c-9d0e1f2a3b4c") // float32, V
val CURR_CHAR_UUID: UUID = UUID.fromString("f3a8b2c3-d4e5-4f6a-7b8c-9d0e1f2a3b4c") // float32, A
val PWR_CHAR_UUID: UUID = UUID.fromString("f3a8b2c4-d4e5-4f6a-7b8c-9d0e1f2a3b4c") // float32, W
val VOLT_SUB_CHAR_UUID: UUID = UUID.fromString("f3a8b2c5-d4e5-4f6a-7b8c-9d0e1f2a3b4c") // float32, V
val OBD_CHAR_UUID: UUID = UUID.fromString("f3a8b2ca-d4e5-4f6a-7b8c-9d0e1f2a3b4c") // OBD-II（チャンク分割）

// デバイス名プレフィックス（ESP32側 device/ble_scan.cpp の "car-iot-%.6s" と対応）
const val DEVICE_NAME_PREFIX = "car-iot-"

// デバイスは DEEP_SLEEP から5分周期で起床し、その都度短時間だけ BLE アドバタイズする
// （esp32_iot_gateway/src/main.cpp の BLE_WAKE_WINDOW_SEC 参照）。起床タイミングを
// またいで捕まえられるよう、接続失敗時はこの時間幅リトライを続ける。
const val CONNECT_RETRY_WINDOW_MS = 5 * 60 * 1000L
const val CONNECT_RETRY_DELAY_MS = 3 * 1000L
