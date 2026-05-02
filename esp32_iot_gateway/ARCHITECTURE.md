# アーキテクチャ概要

## 層構成

3層アーキテクチャ（device / domain / service）で `src/` 以下を管理する。

```
src/
├── main.cpp          エントリポイント。各層を組み合わせてユースケースを実行する
├── config.h          全層から参照可能な定数・宣言（FIRMWARE_VERSION, SLEEP_INTERVAL_SEC 等）
├── config.cpp        config.h の実装（デバイスID取得, NVS アクセス等）
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
| `ble_scan.h/.cpp` | BLE スキャナー（SwitchBot Manufacturer Data 受信、FreeRTOS キュー経由で domain に渡す） |
| `ads.h/.cpp` | ADS1115 I2Cドライバ（差動電圧読み取り） |
| `ina228.h/.cpp` | INA228 I2Cドライバ（電流・電力・温度読み取り） |
| `oled.h/.cpp` | SSD1306 OLEDドライバ（表示制御） |
| `speaker.h/.cpp` | ブザー / スピーカードライバ（tone PWM制御） |

### domain/

ハードウェアにもネットワークにも依存しないビジネスロジック。
device / service を include してはいけない。標準ライブラリのみ使用可。

| ファイル            | 役割                                                           |
|---------------------|----------------------------------------------------------------|
| `measurement.h`     | 計測値の構造体（VoltageReading, PowerReading）                 |
| `telemetry.h/.cpp`  | JSON シリアライズ（buildShadowPayload / buildThermometerPayload / buildCo2Payload）|
| `sensor.h`          | BLE センサー共通構造体（SensorBase）                          |
| `thermometer.h/.cpp`| SwitchBot 温湿度計パーサー（ThermometerData / ThermometerParser）|
| `co2meter.h/.cpp`   | SwitchBot CO2センサーパーサー（Co2MeterData / Co2MeterParser）|
| `sensor_factory.h/.cpp` | センサー種別振り分け（SensorVariant = std::variant）      |
| `ble_targets.h/.cpp`| 監視対象 BLE アドレスの NVS 永続化（BleTargets、NS: "switchbot"）|

### service/

ユースケースの実装。device と domain を組み合わせて目的を達成する。
同層間（service ↔ service）の参照は許容する。

| ファイル | 役割 |
|----------|------|
| `mqtt.h/.cpp` | MQTT publish / subscribe / pollMqtt（device/lte をトランスポートとして使用） |
| `https.h/.cpp` | HTTPS GET（AT+SH* ストリーミング）/ ファイルダウンロード（AT+HTTPTOFS → SIM FS） |
| `ota.h/.cpp` | AWS IoT Jobs 確認・ファームウェア適用・ロールバック管理 |
| `logger.h/.cpp` | シリアルデバッグ出力（横断的関心事） |

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
config.h  （全層から参照可）

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
