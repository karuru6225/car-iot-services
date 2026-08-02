// 計測サービス
const kMeasService  = 'f3a8b2c1-d4e5-4f6a-7b8c-9d0e1f2a3b4c';
const kVoltMainChar = 'f3a8b2c2-d4e5-4f6a-7b8c-9d0e1f2a3b4c';  // float32, V
const kCurrChar     = 'f3a8b2c3-d4e5-4f6a-7b8c-9d0e1f2a3b4c';  // float32, A
const kPwrChar      = 'f3a8b2c4-d4e5-4f6a-7b8c-9d0e1f2a3b4c';  // float32, W
const kVoltSubChar  = 'f3a8b2c5-d4e5-4f6a-7b8c-9d0e1f2a3b4c';  // float32, V
const kObdChar      = 'f3a8b2ca-d4e5-4f6a-7b8c-9d0e1f2a3b4c';  // OBD-II（チャンク分割、OBD.md参照）

// デバイスは DEEP_SLEEP から5分周期で起床し、その都度短時間だけ BLE アドバタイズする
// （esp32_iot_gateway/src/main.cpp の BLE_WAKE_WINDOW_SEC 参照）。起床タイミングを
// またいで捕まえられるよう、接続失敗時はこの時間幅リトライを続ける。
const kConnectRetryWindow = Duration(minutes: 5);
const kConnectRetryDelay  = Duration(seconds: 3);
