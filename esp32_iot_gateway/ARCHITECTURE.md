# アーキテクチャ概要

## 層構成

3層アーキテクチャ（device / domain / service）で `src/` 以下を管理する。

```
src/
├── main.cpp          エントリポイント。各層を組み合わせてユースケースを実行する
├── config.h          全層から参照可能な定数・宣言（FIRMWARE_VERSION, SLEEP_INTERVAL_SEC 等。FIRMWARE_VERSIONはBOARD_VERSIONで基板シリーズ別に分岐）
├── config.cpp        config.h の実装（デバイスID取得, NVS アクセス等）
├── board_pins.h      全層から参照可能なピン配置（BoardPins構造体、boardPins()）。BOARD_VERSIONに応じてboard_pins_v1.h/v2.hをinclude
├── board_pins_v1.h   m5atom_power_adc v1基板の実ピン値
├── board_pins_v2.h   m5atom_power_adc v2基板の実ピン値
├── logger.h/.cpp     全層から参照可能なシリアルデバッグ出力（横断的関心事。実装はservice/log_storage.hに委譲）
├── provision.cpp     プロビジョニング専用（provision env のみビルド。通常ビルドから除外）
├── device/           ハードウェアドライバ層
├── domain/           ビジネスロジック層
└── service/          ユースケース層
```

---

## 各層の役割

### device/

ハードウェアへの直接アクセス。I2C / UART / GPIO / ATコマンドの制御を担う。
上位層（domain / service）を include してはいけない。

| ファイル | 役割 |
|----------|------|
| `lte.h/.cpp` | SIM7080G ATコマンド制御（GPRS接続, 証明書アップロード, 電源管理, ファイル読み取り・削除） |
| `ble_scan.h/.cpp` | BLE スキャナー（SwitchBot Manufacturer Data 受信、FreeRTOS キュー経由で domain に渡す）。`start()`はブロッキング版（menu.cppのBLE登録画面用）、`startAsync()`/`isScanning()`/`stop()`は非同期版（monitor.cpp/main.cppが使用） |
| `ble_peripheral.h/.cpp` | `BlePeripheral` クラス。GATT Peripheral（計測値 Notify + 設定 R/W）、Passkey ペアリング |
| `ads.h/.cpp` | ADS1115 I2Cドライバ（`adsReadDiffMain()` = AIN0/AIN1 = メイン、`adsReadDiffSub()` = AIN2/AIN3 = サブ） |
| `ina228.h/.cpp` | `Ina228` クラス。INA228 I2Cドライバ（電流・電力・温度・積算電荷量の読み取り、電荷リセット） |
| `oled.h/.cpp` | SSD1306 OLEDドライバ（表示制御） |
| `speaker.h/.cpp` | ブザー / スピーカードライバ（tone PWM制御） |
| `button.h/.cpp` | デバウンス・長押し検出（`ButtonEvent`: BTN0_SHORT / BTN0_LONG / BTN1_SHORT / BTN1_LONG）、ピンは`begin()`で`boardPins()`から取得 |
| `can.h/.cpp` | GU0コネクタ経由のTWAI（CAN）ドライバ。Honda N-VAN OBD-II Mode 01（29bit拡張アドレッシング）の送受信、バスオフリカバリ。ISO-TP（ISO 15765-2）マルチフレーム受信（Flow Control自動送信）、複数PIDをまとめて1フレームで要求する多PID送信に対応 |

### domain/

ハードウェアにもネットワークにも依存しないビジネスロジック。
device / service を include してはいけない。標準ライブラリのみ使用可。

| ファイル            | 役割                                                           |
|---------------------|----------------------------------------------------------------|
| `measurement.h`     | 計測値の構造体（VoltageReading, PowerReading）                 |
| `telemetry.h/.cpp`  | `buildConfigPayload`（Shadow reported用JSON）+ `ITelemetryEncoder`（テンプレートメソッド。`JsonTelemetryEncoder`/`MsgPackTelemetryEncoder`が`encodeBattery`/`encodeThermometer`/`encodeCo2`を提供）。MsgPack版は`[magic 0xC1][version][本体][CRC32 4B]`でラップしパケット破損を検知する |
| `sensor.h`          | BLE センサー共通構造体（SensorBase）                          |
| `thermometer.h/.cpp`| SwitchBot 温湿度計パーサー（ThermometerData / ThermometerParser）|
| `co2meter.h/.cpp`   | SwitchBot CO2センサーパーサー（Co2MeterData / Co2MeterParser）|
| `sensor_factory.h/.cpp` | センサー種別振り分け（SensorVariant = std::variant）      |
| `ble_targets.h/.cpp`| 監視対象 BLE アドレスの NVS 永続化（BleTargets、NS: "switchbot"）|
| `sensor_filter.h/.cpp` | BLE センサー値のメディアンフィルタ（`BLE_MEDIAN_FILTER`ビルドフラグ時のみ有効、直近3件のアドレス別履歴を保持）|
| `obd.h/.cpp` | OBD-II Mode 01 PID（全29種）の構造体（`OBDReading`）とデコード関数群。BLE送信用の固定レイアウト構造体（`ObdBlePacket`）と変換関数、多PID応答をPIDセグメント単位に分解する`obdParseMultiResponse()`も含む |
| `charging.h/.cpp` | 充電ヒステリシス判定（`decideCharging()`）。電圧・現在の充電状態・閾値から次の充電状態を返す純粋関数。`main.cpp`の`updateChargingState()`から呼ばれる |
| `url.h/.cpp` | URL文字列をhost/pathに分解する`parseUrl()`。`service/https.cpp`から呼ばれる |

### service/

ユースケースの実装。device と domain を組み合わせて目的を達成する。
同層間（service ↔ service）の参照は許容する。

| ファイル | 役割 |
|----------|------|
| `mqtt.h/.cpp` | MQTT publish / subscribe / pollMqtt（device/lte をトランスポートとして使用） |
| `https.h/.cpp` | HTTPS GET（AT+SH* ストリーミング）/ ファイルダウンロード（AT+HTTPTOFS → SIM FS） |
| `jobs.h/.cpp` | AWS IoT Jobs プロトコル層（subscribe / get-next / report）。OTA・コマンド共通 |
| `ota.h/.cpp` | OTA 固有ロジック（ファームウェア適用・ロールバック管理・前回結果報告）。Jobs プロトコルは jobs.h に委譲 |
| `command.h/.cpp` | Jobs コマンドのディスパッチと実行（`ah_reset`、`charge_main_batt`） |
| `shadow.h/.cpp` | Shadow config publish・delta subscribe・設定変更の適用 |
| `monitor.h/.cpp` | 計測サイクル（`measure()`＝非同期BLEスキャン開始＋アナログ計測 / `collectBle()`＝スキャン完了時のみ収集 / `publishBattery()` / `publishBle()`）・`MeasureResult` 定義 |
| `menu.h/.cpp` | OLED + 2ボタン設定メニュー、`enterMenuMode()` → `OperationMode` を返す |
| `menu_util.h/.cpp` | メニュー用パスユーティリティ（pathPush / pathPop / pathTitle 等） |
| `obdpoll.h/.cpp` | `obdPoll()`。全29PIDを6PIDずつ計5リクエストにバッチ化して問い合わせ（`canInit()`済み前提）。CONTINUOUSモードの1秒ティックから呼ばれる。末尾でMode22 UDS単発リクエスト（DID 0x2201 ATF油温）も実行する |
| `diddscan.h/.cpp` | `didScanRun()`。Mode22 (UDS) DID総当たりスキャン。指定範囲を`22XXYY`で問い合わせ、正常応答とNRC 0x22/0x33ヒットのみ記録する。燃料残量・油温等の未確定DIDを探すための一時的な調査機能で、OLEDメニュー「OBD > DID Scan」から手動実行する |
| `operation_mode.h/.cpp` | `IOperationModeHandler`（`beforeRun()`/`run()`を持つ抽象ハンドラ）+ `OperationModeManager`。`OperationMode` ごとにハンドラインスタンスを登録・ディスパッチするレジストリ（`main.cpp`のloop()分岐を集約）。モード状態自体は持たず`OperationModeContext`を参照する |
| `mode_context.h/.cpp` | `OperationModeContext`。動作モードハンドラ間で共有する実行時状態（現在モード・直近計測値・BLE昇格フラグ等）。状態遷移系プロパティはsetter経由でのみ変更させ、変更時に`[MODE_CTX]`ログを出す |
| `mode_common.h/.cpp` | 複数モードハンドラから呼ばれる横断ヘルパー（`updateChargingState()`/`secsToNextBoundary()`/`pollBleCollect()`/`checkAndHandleJob()`） |
| `mode_deep_sleep.h/.cpp` | `DeepSleepModeHandler`。BLE接続によるCONTINUOUSへの自動昇格判定（`beforeRun()`）とDeepSleep突入処理（`run()`） |
| `mode_continuous_base.h/.cpp` | `ContinuousModeHandlerBase`。CONTINUOUS/TIMED_CONTINUOUS共通の5分待機ループ（`continuousLoopCore()`）とBLE切断による降格判定（`beforeRun()`） |
| `mode_continuous.h/.cpp` | `ContinuousModeHandler`（`ContinuousModeHandlerBase`派生）。無期限CONTINUOUS |
| `mode_timed_continuous.h/.cpp` | `TimedContinuousModeHandler`（`ContinuousModeHandlerBase`派生）。`OperationModeContext`の継続期限まで繰り返し、期限到達で自動DEEP_SLEEP |
| `pubqueue.h/.cpp` | オフラインバッファ（RTC メモリ + SPIFFS）・MQTT publish キュー管理 |
| `log_storage.h/.cpp` | デバッグログの SPIFFS 保存（起動ごと1ファイル、最大12ファイルのリングバッファ）。`logger.h/.cpp`（src直下、横断的関心事）から呼ばれる |

---

## 依存ルール

**上位層が下位層を参照する。逆方向は禁止。**

```
main.cpp
   ↓ 全層を参照可
service/  ←→  service/ （同層間は可）
   ↓
device/
   ↓
config.h / board_pins.h / logger.h  （全層から参照可）

domain/   （どこからでも参照可。自身は何にも依存しない）
```

### include の向き（具体例）

```cpp
// service/mqtt.cpp — OK
#include "mqtt.h"
#include "../device/lte.h"   // 下位層を参照
#include "../config.h"        // 横断的定数を参照

// device/lte.cpp — OK
#include "lte.h"
#include "../config.h"
#include "../logger.h"         // 横断的関心事を参照

// device/lte.cpp — NG
#include "../service/mqtt.h"  // 上位層の参照は禁止
```

---

## 命名規則

| 対象 | 規則 | 例 |
|------|------|----|
| ファイル名 | `snake_case` | `battery.h`, `ota.cpp` |
| クラス名 | `PascalCase` | `Lte`, `BatteryState` |
| 関数名 | `camelCase` | `getMqttHost()`, `readVoltage()` |
| 定数 | `UPPER_SNAKE_CASE` | `SLEEP_INTERVAL_SEC`, `MQTT_PORT` |
| グローバルインスタンス | `camelCase`（型名を短縮） | `lte`, `logger`, `ota` |
