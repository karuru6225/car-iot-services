# プロジェクトコンテキスト

## 概要

車載 IoT システム。M5Atom S3 が ADS1115 で車載バッテリー電圧を測定し、
SwitchBot 防水温湿度計（WoIOSensor）の BLE アドバタイズをスキャンして、
SIM7080G（SORACOM Cat-M）経由で AWS IoT Core に MQTT over TLS で送信する。

クラウド側では S3 + Athena でデータを蓄積し、API Gateway + Lambda 経由で
CloudFront ホスティングの Web 管理画面からグラフ表示・削除操作を行える。

## ハードウェア

| 項目 | 内容 |
|---|---|
| MCU | M5Atom S3（ESP32-S3） |
| ディスプレイ | 内蔵 0.85" IPS LCD 128×128（GC9107） |
| ボタン | GPIO 41（Active-LOW、ハードウェアプルアップ内蔵） |
| ADC | ADS1115（I2C: SDA=G38, SCL=G39, ADDR=0x49） |
| 電圧測定回路 | 差動入力（AIN0-AIN1）、分圧回路 R1=680kΩ / R2=11kΩ、GAIN_EIGHT(±0.512V) |
| センサー | SwitchBot WoIOSensor（BLE アドバタイズのみ、接続不要） |
| LTE | M5Stack U128（SIM7080G CAT-M/NB-IoT）、SORACOM SIM |
| LTE ピン | RX=G6(GPIO6) ← U128 TXD、TX=G5(GPIO5) → U128 RXD |
| 電源 | 車載 12V バッテリー → DC-DC / レギュレータ → M5Atom S3 |

## 動作モード

### 本番モード（デフォルト、`#define DEBUG_MODE` コメントアウト時）

1. BLE スキャン（5 秒）
2. ADS1115 から電圧読み取り + キューから BLE データを収集してペイロード生成
3. LTE 接続確認・再接続（切断時）
4. 全ペイロードを AWS IoT Core に MQTT publish（最大 3 分リトライ）
5. MQTT/GPRS 切断 → DeepSleep 5 分 → `setup()` から再起動

### デバッグモード（`#define DEBUG_MODE` 有効時）

1. BLE スキャン → 電圧測定 → MQTT publish → SEND_INTERVAL_SEC（60 秒）待機
2. 待機中に長押し検出で登録モードへ
3. DeepSleep しない（繰り返しループ）

### 登録モード（デバッグモード中に長押しで起動）

1. BLE スキャンで SwitchBot デバイスを検索
2. 見つかったデバイス一覧を表示（登録済み / 未登録を区別）
3. 短押し: 次の項目へ
4. 長押し: 選択中のデバイスを登録（未登録）または削除（登録済）、または戻る

## ファイル構成

```
car-iot-services/
├── m5atom_iot_gateway/                    デバイス側スケッチ
│   ├── m5atom_iot_gateway.ino             Application: メインループ・モード管理
│   ├── config.h                           定数（スキャン時間・ピン番号・閾値等）
│   ├── certs.h                            AWS IoT Core 証明書・秘密鍵（gitignore）
│   ├── certs.example.h                    証明書のサンプルテンプレート
│   ├── register_mode.h/.cpp               Application Service: 登録ユースケース
│   ├── view.h / view.cpp                  Presentation: Serial + Display 出力
│   ├── domain__switchbot_data.h/.cpp      Value Object: センサーデータ・パース
│   ├── domain__targets.h/.cpp             Aggregate + Repository: 監視対象リスト
│   ├── infra__ble_scan.h/.cpp             Infrastructure: BLE スキャン
│   ├── infra__button.h/.cpp               Infrastructure: GPIO ボタン
│   ├── infra__lte.h/.cpp                  Infrastructure: SIM7080G MQTT over TLS
│   └── infra__logger.h/.cpp               Infrastructure: Serial デバッグ出力
├── m5atom_power_adc/                      KiCad PCB プロジェクト（電源・ADC 外付け基板）
│   ├── m5atom_power_adc.kicad_pro
│   ├── m5atom_power_adc.kicad_sch
│   └── m5atom_power_adc.kicad_pcb
├── infra/                                 クラウドインフラ（Terraform）
│   ├── main.tf                            プロバイダ・IoT エンドポイント
│   ├── iot.tf                             IoT Core: Thing・証明書・Policy・Topic Rule
│   ├── s3.tf                              S3・Glue・Athena
│   ├── lambda.tf                          Lambda 4 本 + API Gateway HTTP API
│   ├── iam.tf                             IAM ロール・ポリシー
│   ├── web.tf                             S3(Web) + CloudFront
│   ├── variables.tf / outputs.tf
│   └── lambda_src/
│       ├── ingest/index.py                IoT Core → S3 書き込み
│       ├── query/index.py                 Athena 非同期クエリ発行・結果取得
│       ├── delete/index.py                Athena で対象特定 → S3 削除
│       └── authorizer/index.py            x-api-key 検証
├── web/
│   └── index.html                         Web 管理画面（単一ファイル SPA）
├── ARCHITECTURE.md
└── CONTEXT.md
```

## データフロー

```
M5Atom S3
  → MQTT over TLS（SIM7080G）
  → AWS IoT Core  topic: sensors/{device_id}/data
  → Topic Rule SQL: SELECT *, topic(2) AS device_id FROM 'sensors/+/data'
  → Lambda ingest
  → S3: raw/year=YYYY/month=MM/day=DD/hour=HH/{device_id}-{uuid8}.json

Web 管理画面
  → GET /data?hours=24  → Lambda query → Athena（非同期ポーリング）→ S3
  → DELETE /data?addr=XX:XX:XX → Lambda delete → Athena → S3
  ※ 認証: x-api-key ヘッダを Lambda authorizer で検証
```

## MQTT ペイロード形式

### 電圧（battery タイプ）

```json
{"type":"battery","id":"voltage_1","voltage":12.34,"ts":"2026-03-05T12:00:00Z"}
```

### 温湿度（thermometer タイプ）

```json
{"type":"thermometer","addr":"AA:BB:CC:DD:EE:FF","temp":25.0,"humidity":60,"battery":80,"rssi":-70,"ts":"2026-03-05T12:00:00Z"}
```

ingest Lambda で `sensor_type` フィールドを付加して S3 に保存する（`"voltage"` / `"switchbot"`）。

## S3 / Glue テーブルスキーマ

| カラム | 型 | 説明 |
|---|---|---|
| `ts` | string | ISO8601 UTC タイムスタンプ |
| `device_id` | string | IoT Thing 名（topic から抽出） |
| `sensor_type` | string | `"voltage"` / `"switchbot"` |
| `voltage` | double | 電圧（V） |
| `id` | string | 電圧センサー識別子（`"voltage_1"` 等） |
| `addr` | string | BLE MAC アドレス |
| `temp` | double | 温度（°C） |
| `humidity` | int | 湿度（%） |
| `battery` | int | BLE デバイスバッテリー（%） |
| `rssi` | int | RSSI（dBm） |
| `year/month/day/hour` | string | Hive パーティション（プロジェクション） |

## 主要クラスとインスタンス

| インスタンス | 型 | 役割 |
|---|---|---|
| `targets` | `Targets` | 監視対象 BLE アドレスの管理・NVS 読み書き |
| `scanner` | `BleScanner` | BLE スキャン・FreeRTOS キュー管理 |
| `button` | `Button` | ISR ベースのボタンイベント検出 |
| `regMode` | `RegisterMode` | 登録モードのロジック |
| `view` | `View` | 出力（`MultiOutput MOut` を内部に持つ） |
| `lte` | `Lte` | SIM7080G 制御・MQTT over TLS publish |
| `logger` | `Logger` | Serial デバッグ出力（`printf`/`println`） |

## 重要な設計決定

### MultiOutput（Serial + Display 同時出力）

`view.cpp` 内部の `static MultiOutput MOut` が `Print` を継承し、
`write()` で Serial と M5.Display 両方に書き込む。
外部からは `view.sensorData(d)` 等のメソッドのみ見える。

### BLE コールバックと FreeRTOS キュー

`SwitchBotCallback::onResult()` は BLE タスクから呼ばれる。
スレッドセーフな `xQueueSend(scanner.queue, &d, 0)` でメインタスクに渡す。
メインタスクは `xQueueReceive` でキューを消費して送信する。

### ボタン ISR

`btnISR()` は `IRAM_ATTR` 付きの静的関数。
Arduino の制約でメンバ関数にできないため、ファイルスコープの
`static volatile bool sLongFired / sShortFired` で状態を持つ。

### NVS 永続化

`Preferences` ライブラリで namespace `"switchbot"` に保存。
キー `"count"` + `"a0"` ～ `"a9"` で最大 10 件の MAC アドレスを管理。

### SwitchBotData のパース

`SwitchBotData::parse()` が Manufacturer Data / Service Data の生バイト列を
Value Object に変換する。Infrastructure 層にはパースロジックを置かない。

パースフォーマット（WoIOSensor）:

```text
Manufacturer Data:
  [0-1]  Company ID (0x0969)
  [2-7]  MAC アドレス
  [10]   温度小数部 (bit3-0)
  [11]   温度整数部 (bit6-0) + 符号 (bit7: 1=正)
  [12]   湿度 (%)
Service Data:
  [2]    バッテリー (%)
```

### LTE / MQTT（SIM7080G + AWS IoT Core）

- TinyGSM ライブラリで AT コマンド制御
- SIM7080G 内蔵 MQTT クライアント（AT+SMCONN / AT+SMPUB）を使用
- TLS 接続に必要な CA 証明書・クライアント証明書・秘密鍵を `certs.h` に格納し、
  起動時にモデムのファイルシステムへ書き込む
- DeepSleep 前に必ず `lte.disconnect()`（MQTT+GPRS 切断）を呼ぶ。
  呼ばないと復帰後に SMSTATE=1 誤認のまま publish してデータが届かない
- APN 設定は `CFUN=0 → CGDCONT → CFUN=1` の順（必須）
- `modem.restart()` は使わず `modem.init()` を使う

### Athena 非同期クエリ（query Lambda）

1. `GET /data?hours=24` で execution_id を返す
2. Web 側が 2 秒ポーリングで `GET /data?execution_id=xxx` を叩く
3. `SUCCEEDED` になったら結果を返す

### include 順序（M5Unified / BLE）

M5Unified を BLE ヘッダより先に include しないとコンフリクトが発生する。
`view.h` が先頭で `<M5Unified.h>` を取り込むため、
`#include "view.h"` を最初に書くことで順序を保証している。

## config.h 定数一覧

| 定数 | 値 | 用途 |
| --- | --- | --- |
| `SWITCHBOT_COMPANY_ID` | `0x0969` | BLE フィルタリング |
| `SCAN_TIME` | `5` | BLE スキャン秒数 |
| `QUEUE_SIZE` | `20` | FreeRTOS キューサイズ |
| `MAX_TARGETS` | `10` | 登録可能デバイス上限 |
| `MAX_FOUND` | `20` | スキャンで見つかるデバイス上限 |
| `BTN_PIN` | `41` | ボタン GPIO 番号 |
| `LONG_PRESS_MS` | `1000` | 長押し判定ミリ秒 |
| `DEBOUNCE_MS` | `50` | チャタリング除去ミリ秒 |
| `SLEEP_INTERVAL_SEC` | `300` | Deep Sleep 間隔（秒）※ .ino 内で直接指定しているため現在未使用 |

infra__lte.h の定数:

| 定数 | 値 | 用途 |
| --- | --- | --- |
| `LTE_RX_PIN` | `6` | G6 ← U128 TXD |
| `LTE_TX_PIN` | `5` | G5 → U128 RXD |
| `APN` | `"soracom.io"` | SORACOM APN |
| `SEND_INTERVAL_SEC` | `60` | デバッグモード送信間隔（秒） |

## ディスプレイ制約

フォント: `efontJA_12`（ASCII 6px幅、日本語 12px幅）、表示幅 128px

- ASCII のみ: 最大 21 文字/行
- MAC アドレス `XX:XX:XX:XX:XX:XX`: 17 文字 = 102px（収まる）
- 日本語 + ASCII 混在は折り返し発生に注意

## ビルド環境

| 項目 | 内容 |
|---|---|
| IDE | Arduino IDE 2.x / VS Code + Arduino 拡張 |
| ボードパッケージ | M5Stack ESP32 (m5stack) 3.2.5 |
| 主要ライブラリ | M5Unified, ESP32 BLE Arduino, Preferences, Adafruit ADS1X15, TinyGSM |
| Arduino 制約 | サブディレクトリの .cpp は自動コンパイルされない → 全 .cpp をルートに配置 |
| Terraform | >= 1.5、AWS プロバイダ ~> 5.0 |
| Lambda ランタイム | Python 3.12 |

## 作業中・引き継ぎ事項

### SwitchBot CO2センサー対応（作業中・BLEフォーマット確認待ち）

WoIOSensor（防水温湿度計）に加えて **SwitchBot CO2センサー** を追加で対応したい。

**ステータス**: BLE Manufacturer Data / Service Data のバイトフォーマットが不明なため実装保留中。
以下のどちらかで確認が必要：

- 実機の `getServiceData()` / `getManufacturerData()` を `Serial.printf("%02X ", ...)` でダンプして確認する
- [OpenWonderLabs/SwitchBotAPI-BLE](https://github.com/OpenWonderLabs/SwitchBotAPI-BLE) の CO2センサー該当ページを確認する

**判明していること**:
- Company ID は WoIOSensor と同じ `0x0969` → 既存の BLE フィルタはそのまま流用できる
- Service Data `[0]` がデバイス種別バイトのため、そこで WoIOSensor と CO2センサーを識別できるはず
- CO2 値（ppm）は Service Data の上位バイトに big-endian uint16 で入っていると推測

**確定したら行う変更**:

| ファイル | 変更内容 |
|---|---|
| `domain__switchbot_data.h` | `uint16_t co2` フィールド追加、デバイス種別識別フラグ |
| `domain__switchbot_data.cpp` | `parse()` でデバイス種別を判定し CO2 をパース |
| `m5atom_iot_gateway.ino` | ペイロード JSON に `"co2"` フィールドを追加 |
| `view.cpp` | CO2 値のディスプレイ表示 |
| `infra/s3.tf` | Glue テーブルスキーマに `co2` カラム（int）を追加 |
| `infra/lambda_src/ingest/index.py` | `sensor_type` に `"switchbot_co2"` を追加（または `switchbot` のまま CO2 フィールドを含める） |
| `web/index.html` | CO2 グラフ追加 |

**MQTT ペイロード追加予定形式**:

```json
{"type":"co2meter","addr":"AA:BB:CC:DD:EE:FF","temp":25.0,"humidity":60,"co2":800,"battery":80,"rssi":-70,"ts":"2026-03-05T12:00:00Z"}
```

---

## 将来の拡張候補

- **マルチコア化**: LTE 送信が不安定なことが判明した時点で対応
  - Core 0: BLE scan タスク（常時スキャン）
  - Core 1: 表示・ボタン・LTE タスク
- **複数センサー種別**: `IDeviceParser` インターフェースを Domain 層に追加し、DI で対応
- **Web 管理画面**: デバイス設定（登録アドレス）のリモート管理
