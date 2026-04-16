# プロジェクトコンテキスト

## 概要

車載 IoT システム。M5Atom S3 が ADS1115 で車載バッテリー電圧を測定し、
SwitchBot BLE センサー（WoIOSensor 防水温湿度計 / CO2センサー）の BLE アドバタイズをスキャンして、
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
| センサー | SwitchBot WoIOSensor（温湿度計）/ CO2センサー（BLE アドバタイズのみ、接続不要） |
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
├── m5atom_iot_gateway/                    デバイス側（PlatformIO プロジェクト）
│   ├── platformio.ini                     PlatformIO ビルド設定（C++17 有効、extra_scripts 設定）
│   ├── extra_scripts.py                   ビルド前フック: git hash を GIT_HASH マクロとして注入
│   ├── src/
│   │   ├── m5atom_iot_gateway.ino         Application: メインループ・モード管理
│   │   ├── config.h                       定数（スキャン時間・ピン番号・閾値等）
│   │   ├── certs.h                        AWS IoT Core 証明書・秘密鍵（gitignore）
│   │   ├── certs.example.h                証明書のサンプルテンプレート
│   │   ├── domain/
│   │   │   ├── sensor.h                   Value Object 基底: SensorBase 構造体
│   │   │   ├── thermometer.h/.cpp         Value Object: 温湿度計データ・パース
│   │   │   ├── co2meter.h/.cpp            Value Object: CO2センサーデータ・パース
│   │   │   ├── sensor_factory.h/.cpp      Factory: センサー種別判定・パーサー振り分け
│   │   │   └── targets.h/.cpp             Aggregate + Repository: 監視対象リスト
│   │   ├── infra/
│   │   │   ├── ble_scan.h/.cpp            Infrastructure: BLE スキャン
│   │   │   ├── button.h/.cpp              Infrastructure: GPIO ボタン
│   │   │   ├── lte.h/.cpp                 Infrastructure: SIM7080G MQTT over TLS
│   │   │   └── logger.h/.cpp              Infrastructure: Serial デバッグ出力
│   │   └── app/
│   │       ├── view.h/.cpp                Presentation: Serial + Display 出力
│   │       ├── register_mode.h/.cpp       Application Service: 登録ユースケース
│   │       └── bypass_mode.h/.cpp         AT コマンド透過モード（デバッグ用）
├── m5atom_power_adc/                      KiCad PCB プロジェクト（電源・ADC 外付け基板）
│   ├── CIRCUIT.md                         回路設計仕様書
│   ├── m5atom_power_adc.kicad_pro
│   ├── m5atom_power_adc.kicad_sch
│   └── m5atom_power_adc.kicad_pcb
├── infra/                                 クラウドインフラ（Terraform）
│   ├── main.tf                            プロバイダ・IoT エンドポイント
│   ├── iot.tf                             IoT Core: Thing・証明書・Policy・Topic Rule
│   ├── s3.tf                              S3・Glue・Athena
│   ├── lambda.tf                          Lambda 3 本 + API Gateway HTTP API（JWT Authorizer）
│   ├── cognito.tf                         Cognito User Pool・App Client・Hosted UI
│   ├── iam.tf                             IAM ロール・ポリシー
│   ├── web.tf                             S3(Web) + CloudFront + ACM + Route53
│   ├── variables.tf / outputs.tf
│   ├── manage.ps1                         デプロイスクリプト（-Profile で aws login 対応）
│   ├── gen_certs.ps1                      証明書生成スクリプト（Secrets Manager → certs.h）
│   └── lambda_src/
│       ├── ingest/index.py                IoT Core → S3 書き込み
│       ├── query/index.py                 Athena 非同期クエリ発行・結果取得
│       └── delete/index.py                S3 直接削除（s3_keys）または Athena で対象特定 → S3 削除
├── web/
│   └── index.html                         Web 管理画面（単一ファイル SPA）
├── docs/
│   └── obd2_honda_nvan.md                 Honda N-VAN OBD2 PID 調査メモ
├── tools/                                 開発補助ツール
│   ├── kicad-mcp/                         KiCad 用 MCP サーバー（Claude Code 連携）
│   ├── kicad-mcp-server/                  KiCad MCP サーバー（別実装）
│   └── kicad-netlist-tool/                KiCad ネットリスト解析ツール
├── rtx830_filter_updater/                 RTX830 フィルタ更新スクリプト（予定）
├── ARCHITECTURE.md
├── CONTEXT.md
├── HARDWARE.md                            新 PCB ハードウェア設計仕様（m5atom_power_adc）
└── SIM7080G.md                            SIM7080G AT コマンドリファレンス
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
      ※ 各行に s3_key（Athena $path 疑似カラム）を含む
  → DELETE /data  body: {"s3_keys": [...]}  → Lambda delete → S3 直接削除
  ※ 認証: Cognito JWT（API Gateway JWT Authorizer）、Hosted UI でログイン
  ※ タイムスタンプは JST 表示（Web 側で変換）
```

## MQTT ペイロード形式

### 電圧（battery タイプ）

```json
{"type":"battery","id":"voltage_1","voltage":12.34,"fw":"1.0.0+a364343b","ts":"2026-03-05T12:00:00Z"}
```

### 温湿度（thermometer タイプ）

```json
{"type":"thermometer","addr":"AA:BB:CC:DD:EE:FF","temp":25.0,"humidity":60,"battery":80,"rssi":-70,"mf":"09b0e9...","fw":"1.0.0+a364343b","ts":"2026-03-05T12:00:00Z"}
```

### CO2（co2meter タイプ）

```json
{"type":"co2meter","addr":"AA:BB:CC:DD:EE:FF","temp":25.0,"humidity":60,"co2":612,"battery":80,"rssi":-70,"mf":"09b0e9...","fw":"1.0.0+a364343b","ts":"2026-03-05T12:00:00Z"}
```

`mf` フィールドは Manufacturer Data の生バイト列を小文字 hex 文字列化したもの（デバッグ用途）。

## S3 / Glue テーブルスキーマ

| カラム | 型 | 説明 |
|---|---|---|
| `ts` | string | ISO8601 UTC タイムスタンプ |
| `type` | string | `"battery"` / `"thermometer"` / `"co2meter"`（デバイス送信値そのまま） |
| `device_id` | string | IoT Thing 名（topic から抽出） |
| `voltage` | double | 電圧（V） |
| `id` | string | 電圧センサー識別子（`"voltage_1"` 等） |
| `addr` | string | BLE MAC アドレス |
| `temp` | double | 温度（°C） |
| `humidity` | int | 湿度（%） |
| `battery` | int | BLE デバイスバッテリー（%） |
| `rssi` | int | RSSI（dBm） |
| `co2` | int | CO2 濃度（ppm）※ co2meter のみ |
| `mf` | string | Manufacturer Data hex 文字列（デバッグ用） |
| `fw` | string | ファームウェアバージョン（例: `"1.0.0+a364343b"`） |
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

ドメイン型:

| 型 | 役割 |
| --- | --- |
| `SensorBase` | FreeRTOS キュー安全な基底 struct（virtual 禁止） |
| `ThermometerData : SensorBase` | 温湿度計データ |
| `Co2MeterData : SensorBase` | CO2センサーデータ（`uint16_t co2` 追加） |
| `SensorVariant` | `std::variant<ThermometerData, Co2MeterData>` FreeRTOS キュー要素 |
| `ThermometerParser` | 温湿度計パース（`parseCommon` は Co2MeterParser も使用） |
| `Co2MeterParser` | CO2センサーパース（`parseCommon` を流用） |
| `SensorParserFactory` | serviceData[0] でデバイス種別を判定してパーサー振り分け |

## 重要な設計決定

### MultiOutput（Serial + Display 同時出力）

`view.cpp` 内部の `static MultiOutput MOut` が `Print` を継承し、
`write()` で Serial と M5.Display 両方に書き込む。
外部からは `view.thermometerData(d)` / `view.co2Data(d)` 等のメソッドのみ見える。

### BLE コールバックと FreeRTOS キュー（std::variant）

`SwitchBotCallback::onResult()` は BLE タスクから呼ばれる。
`SensorParserFactory::parse()` でデバイス種別を判定し、`SensorVariant`（`std::variant<ThermometerData, Co2MeterData>`）
に格納して `xQueueSend(scanner.queue, &v, 0)` でメインタスクに渡す。

FreeRTOS キューは `memcopy` ベースのため virtual 関数（vtable/vptr）を持つ型は使用不可。
`std::variant` は trivially copyable かつ型安全で、これを回避する最適解。

メインタスクでは `std::visit` + `if constexpr` で型ごとに処理を分岐する:

```cpp
std::visit([&](auto &&d) {
  using T = std::decay_t<decltype(d)>;
  if constexpr (std::is_same_v<T, ThermometerData>) { /* 温湿度計処理 */ }
  else if constexpr (std::is_same_v<T, Co2MeterData>) { /* CO2処理 */ }
}, v);
```

### センサーパーサー設計（継承 + ファクトリー）

```text
ThermometerParser::parseCommon()  ← Co2MeterParser も呼び出す（mfHex変換・温湿度・バッテリー）
ThermometerParser::parse()        ← ThermometerData を返す
Co2MeterParser::parse()           ← parseCommon + mfData[15-16] big-endian で CO2 ppm
SensorParserFactory::parse()      ← sd[0] == 0x35 なら CO2、それ以外は温湿度計
```

### CO2センサー BLE フォーマット（実機確認済み）

```text
Manufacturer Data:
  [0-1]  Company ID (0x0969) ← WoIOSensor と共通
  [2-7]  MAC アドレス
  [10]   温度小数部 (bit3-0)
  [11]   温度整数部 (bit6-0) + 符号 (bit7: 1=正)
  [12]   湿度 (%)
  [15-16] CO2 濃度 ppm（big-endian uint16）
Service Data:
  [0]    デバイス種別: 0x35 = CO2センサー（WoIOSensor は 0x54 等）
  [2]    バッテリー (%)
```

### ボタン ISR

`btnISR()` は `IRAM_ATTR` 付きの静的関数。
Arduino の制約でメンバ関数にできないため、ファイルスコープの
`static volatile bool sLongFired / sShortFired` で状態を持つ。

### NVS 永続化

`Preferences` ライブラリで namespace `"switchbot"` に保存。
キー `"count"` + `"a0"` ～ `"a9"` で最大 10 件の MAC アドレスを管理。

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

1. `GET /data?hours=24[&type=battery|thermometer|co2meter]` で execution_id を返す
2. Web 側が 2 秒ポーリングで `GET /data?execution_id=xxx` を叩く
3. `SUCCEEDED` になったら結果を返す。各行に `s3_key`（Athena の `$path` 疑似カラム）を含む

### 行単位削除（delete Lambda）

`DELETE /data` に JSON body `{"s3_keys": ["s3://bucket/raw/..."]}` を渡すと、
Athena を使わず指定キーを直接 S3 削除する。
クエリ結果の `s3_key` を使うため、選択した行だけを正確に削除できる。

### include 順序（M5Unified / BLE）

M5Unified を BLE ヘッダより先に include しないとコンフリクトが発生する。
`view.h` が先頭で `<M5Unified.h>` を取り込むため、
`#include "view.h"` を最初に書くことで順序を保証している。

## config.h 定数一覧

| 定数 | 値 | 用途 |
| --- | --- | --- |
| `FIRMWARE_VERSION` | `"1.0.0+" GIT_HASH` | ファームウェアバージョン（ペイロード `fw` フィールド） |
| `GIT_HASH` | ビルド時注入（8文字 hex） | git hash 先頭 8 文字。`extra_scripts.py` が `-DGIT_HASH` で定義 |
| `SWITCHBOT_COMPANY_ID` | `0x0969` | BLE フィルタリング |
| `SCAN_TIME` | `10` | BLE スキャン秒数 |
| `QUEUE_SIZE` | `20` | FreeRTOS キューサイズ |
| `MAX_TARGETS` | `10` | 登録可能デバイス上限 |
| `MAX_FOUND` | `20` | スキャンで見つかるデバイス上限 |
| `BTN_PIN` | `41` | ボタン GPIO 番号 |
| `LONG_PRESS_MS` | `1000` | 長押し判定ミリ秒 |
| `DEBOUNCE_MS` | `50` | チャタリング除去ミリ秒 |
| `SLEEP_INTERVAL_SEC` | `300` | Deep Sleep 間隔（秒） |
| `PAYLOAD_BATTERY_SIZE` | `128` | battery ペイロードバッファサイズ（バイト） |
| `PAYLOAD_SENSOR_SIZE` | `256` | sensor ペイロードバッファサイズ（バイト、mf hex 込み） |

infra/lte.h の定数:

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
| IDE | PlatformIO（Arduino IDE は非対応に変更） |
| プラットフォーム | espressif32 |
| ボード | m5stack-atoms3 |
| C++ 標準 | C++17（`-std=gnu++17`）※ `std::variant` に必須 |
| ビルドフック | `extra_scripts.py`（`pre:`）— git hash を `GIT_HASH` マクロとして `-D` フラグで注入 |
| 主要ライブラリ | M5Unified, ESP32 BLE Arduino, Preferences, Adafruit ADS1X15, TinyGSM |
| BLE API | PlatformIO では `getManufacturerData()` / `getServiceData()` が `std::string` を返す（Arduino IDE は `String`） |
| Terraform | >= 1.5、AWS プロバイダ ~> 5.0 |
| Lambda ランタイム | Python 3.12 |

## 将来の拡張候補

- **マルチコア化**: LTE 送信が不安定なことが判明した時点で対応
  - Core 0: BLE scan タスク（常時スキャン）
  - Core 1: 表示・ボタン・LTE タスク
- **Web 管理画面**: デバイス設定（登録アドレス）のリモート管理
- **DDoS・コスト対策強化**（不特定多数公開時）:
  - API も CloudFront 経由に統一し、API Gateway に Resource Policy で直接アクセスを禁止
  - CloudFront に AWS WAF を付けてレートリミットを適用（$5/月〜）
  - これにより WAF を1箇所に集約しつつ API Gateway へのコスト攻撃を防止できる
  - 現状は API GW スロットリングのみ適用（個人用途では十分）
