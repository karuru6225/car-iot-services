# プロジェクトコンテキスト

## 概要

車載 IoT システム。ESP32-S3 が ADS1115 で 2 系統の車載バッテリー電圧を、
INA228 でサブバッテリーの電流・電力・温度を計測し、
SIM7080G（SORACOM Cat-M）経由で AWS IoT Core に MQTT over TLS で送信する。

クラウド側では S3 + Athena でデータを蓄積し、API Gateway + Lambda 経由で
CloudFront ホスティングの Web 管理画面からグラフ表示・削除操作を行える。

> **旧機種**: M5Atom S3 ベースの `m5atom_iot_gateway` は段階的廃止予定。
> アクティブな開発は `esp32_iot_gateway` で行う。

## ハードウェア（esp32_iot_gateway）

| 項目 | 内容 |
| ---- | ---- |
| MCU | ESP32-S3-MINI-1-N8（`m5atom_power_adc` 基板直付け） |
| ADC | ADS1115（I2C: SDA=GPIO17, SCL=GPIO18, ADDR=0x48） |
| 電圧測定 ch0 | 差動入力（AIN0-AIN1）、分圧回路 680kΩ / 22kΩ — **サブバッテリー** |
| 電圧測定 ch1 | 差動入力（AIN2-AIN3）、分圧回路 680kΩ / 22kΩ — **メインバッテリー** |
| 電流計 | INA228（I2C: SDA=GPIO17, SCL=GPIO18, ADDR=0x40） |
| OLED | SSD1306 128×64（I2C: SDA=GPIO17, SCL=GPIO18, ADDR=0x3C） |
| LTE | M5Stack U128（SIM7080G CAT-M/NB-IoT）、SORACOM SIM |
| LTE ピン | RX=GPIO7 ← U128 TXD、TX=GPIO8 → U128 RXD |
| LTE 電源 | GPIO9（LTE_EN: HIGH=ON、AO3401A パワースイッチ経由） |
| 電源 | 車載 12V → LM2596（12V→5V）→ AMS1117-3.3（5V→3.3V）→ ESP32 |

## 動作モード（esp32_iot_gateway）

### 本番モード（デフォルト、`#define DEBUG_MODE` コメントアウト時）

1. 起動 → LTE 接続 → 時刻同期
2. AWS IoT Jobs で OTA チェック（更新あれば適用・自動再起動）
3. MQTT 接続確認 → `esp_ota_mark_app_valid_cancel_rollback()`
4. センサー計測（ADS1115 電圧 × 2、INA228 電流・電力・温度）
5. AWS IoT Shadow に計測値を publish
6. LTE 切断 → DeepSleep 5 分 → `setup()` から再起動

### デバッグモード（`#define DEBUG_MODE` 有効時）

1. 上記 1〜5 を実行後、DeepSleep せずにループ
2. loop() でセンサー値を OLED に表示し続ける

## ファイル構成

```text
car-iot-services/
├── esp32_iot_gateway/                     ESP32-S3-MINI-1 ゲートウェイ（アクティブ）
│   ├── platformio.ini                     ビルド設定（SPIFFS 有効・QIO パッチ）
│   ├── extra_scripts.py                   ビルドフック: git hash 注入
│   ├── sdkconfig.defaults                 OTA ロールバック有効化
│   ├── partitions_two_ota.csv             デュアルバンク OTA パーティション構成（8MB）
│   ├── ARCHITECTURE.md                    3層アーキテクチャ（device/domain/service）
│   ├── CONTEXT.md                         このプロジェクト固有のコンテキスト
│   ├── OTA.md                             OTA 仕様ドキュメント
│   └── src/
│       ├── main.cpp                       起動 → LTE → OTA チェック → Shadow 送信 → DeepSleep
│       ├── config.h/.cpp                  定数・NVS アクセス（device ID / mqtt_host / cert CRC / job ID）
│       ├── device/                        ハードウェアドライバ層
│       │   ├── lte.h/.cpp                 SIM7080G ATコマンド・GPRS・HTTPS OTA
│       │   ├── ads.h/.cpp                 ADS1115 差動電圧読み取り
│       │   ├── ina228.h/.cpp              INA228 電流・電力・温度
│       │   ├── oled.h/.cpp                SSD1306 OLED 表示
│       │   └── speaker.h/.cpp             ブザー PWM 制御
│       ├── domain/                        ビジネスロジック層
│       │   ├── measurement.h              計測値構造体（VoltageReading / PowerReading）
│       │   └── telemetry.h/.cpp           AWS Shadow ペイロード JSON 組み立て
│       └── service/                       ユースケース層
│           ├── mqtt.h/.cpp                MQTT publish / subscribe / poll
│           ├── ota.h/.cpp                 AWS IoT Jobs OTA（$next/get・状態管理・ロールバック）
│           └── logger.h/.cpp              Serial デバッグ出力
├── m5atom_iot_gateway/                    M5Atom S3 ゲートウェイ（段階的廃止予定）
├── m5atom_power_adc/                      KiCad PCB プロジェクト（電源・ADC 外付け基板）
│   ├── CIRCUIT.md                         回路設計仕様書
│   ├── m5atom_power_adc.kicad_pro
│   ├── m5atom_power_adc.kicad_sch         メインシート（1xx 番台）
│   ├── GroveUnit.kicad_sch                サブシート: Grove ユニット制御（5xx/6xx 番台）
│   ├── RelayControl.kicad_sch             サブシート: リレー制御（2xx/3xx/4xx 番台）
│   ├── VoltageSense.kicad_sch             サブシート: 電圧検出（7xx/8xx 番台）
│   ├── Library.pretty/                    カスタムフットプリント（MLT-8530 等）
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
│   ├── gen_certs.ps1                      証明書生成スクリプト（Secrets Manager → certs.h）※m5atom 用
│   ├── provision_device.sh / .ps1         ESP32 初回プロビジョニング（MAC→ID生成・証明書発行・SPIFFS書込）
│   ├── deploy_ota.sh / .ps1               OTA デプロイ（ビルド→S3アップロード→IoT Job作成）
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

```text
ESP32-S3
  → MQTT over TLS（SIM7080G）
  → AWS IoT Core  topic: $aws/things/{device_id}/shadow/update
  → Topic Rule SQL: SELECT *, topic(3) AS device_id FROM '$aws/things/+/shadow/update'
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

### デバイスシャドウ更新（esp32_iot_gateway）

トピック: `$aws/things/{device_id}/shadow/update`

```json
{"state":{"reported":{"v1":12.34,"v2":12.10,"current":5.2100,"power":62.500,"temp":28.5,"ts":1746143400}}}
```

| フィールド | 型 | 内容 |
| --- | --- | --- |
| `v1` | float | サブバッテリー電圧（V） |
| `v2` | float | メインバッテリー電圧（V） |
| `current` | float | サブバッテリー電流（A） |
| `power` | float | サブバッテリー電力（W） |
| `temp` | float | INA228 内蔵温度センサー（°C） |
| `ts` | int | UNIX タイムスタンプ（秒） |

### 旧形式（m5atom_iot_gateway、段階的廃止予定）

```json
{"type":"battery","id":"voltage_1","voltage":12.34,"fw":"1.0.0+a364343b","ts":"2026-03-05T12:00:00Z"}
{"type":"thermometer","addr":"AA:BB:CC:DD:EE:FF","temp":25.0,"humidity":60,"battery":80,"rssi":-70,"mf":"09b0e9...","fw":"1.0.0+a364343b","ts":"2026-03-05T12:00:00Z"}
{"type":"co2meter","addr":"AA:BB:CC:DD:EE:FF","temp":25.0,"humidity":60,"co2":612,"battery":80,"rssi":-70,"mf":"09b0e9...","fw":"1.0.0+a364343b","ts":"2026-03-05T12:00:00Z"}
```

## S3 / Glue テーブルスキーマ

| カラム | 型 | 説明 |
| --- | --- | --- |
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

## 主要クラスとインスタンス（esp32_iot_gateway）

| インスタンス | 型 | 役割 |
| --- | --- | --- |
| `lte` | `Lte` | SIM7080G ATコマンド制御・GPRS接続・証明書管理・HTTPS OTA |
| `mqtt` | `Mqtt` | MQTT publish / subscribe / pollMqtt |
| `ota` | `Ota` | AWS IoT Jobs 確認・FW 適用・ロールバック管理 |
| `logger` | `Logger` | Serial デバッグ出力（`printf`/`println`） |

ドメイン型:

| 型 | 役割 |
| --- | --- |
| `VoltageReading` | 電圧計測値（`float voltage`） |
| `PowerReading` | 電力計測値（`float current, power, temp`） |

## 重要な設計決定

### LTE / MQTT（SIM7080G + AWS IoT Core）

- TinyGSM ライブラリで AT コマンド制御
- SIM7080G 内蔵 MQTT クライアント（AT+SMCONN / AT+SMPUB）を使用
- TLS 証明書は SPIFFS に保存（`/certs/ca.crt` 等）、起動時に CRC チェックして変更時のみモデムへ再アップロード
- DeepSleep 前に必ず `lte.disconnect()`（MQTT+GPRS 切断）を呼ぶ
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

## config.h 定数一覧（esp32_iot_gateway）

| 定数 | 値 | 用途 |
| --- | --- | --- |
| `FIRMWARE_VERSION` | `"1.0.0+" GIT_HASH` | ファームウェアバージョン |
| `GIT_HASH` | ビルド時注入（8文字 hex） | `extra_scripts.py` が `-DGIT_HASH` で定義 |
| `SLEEP_INTERVAL_SEC` | `300` | DeepSleep 間隔（秒） |
| `CERT_PATH_CA` | `"/certs/ca.crt"` | SPIFFS 上の CA 証明書パス |
| `CERT_PATH_DEVICE` | `"/certs/device.crt"` | SPIFFS 上のデバイス証明書パス |
| `CERT_PATH_KEY` | `"/certs/device.key"` | SPIFFS 上の秘密鍵パス |
| `MQTT_PORT` | `8883` | AWS IoT Core MQTT ポート |

device/lte.h の定数:

| 定数 | 値 | 用途 |
| --- | --- | --- |
| `LTE_RX_PIN` | `7` | GPIO7 ← U128 TXD |
| `LTE_TX_PIN` | `8` | GPIO8 → U128 RXD |
| `LTE_EN_PIN` | `9` | GPIO9（AO3401A パワースイッチ制御） |
| `APN` | `"soracom.io"` | SORACOM APN |
| `SEND_INTERVAL_SEC` | `60` | デバッグモード送信間隔（秒） |

## ビルド環境（esp32_iot_gateway）

| 項目 | 内容 |
| ---- | ---- |
| IDE | PlatformIO |
| プラットフォーム | espressif32 |
| ボード | esp32-s3-devkitc-1（ESP32-S3-MINI-1 互換） |
| C++ 標準 | C++17（`-std=gnu++17`） |
| ビルドフック | `extra_scripts.py`（`pre:`）— git hash を `GIT_HASH` マクロとして注入 |
| 主要ライブラリ | TinyGSM, ArduinoJson, Adafruit SSD1306, Adafruit GFX, Adafruit ADS1X15 |
| Terraform | >= 1.5、AWS プロバイダ ~> 5.0 |
| Lambda ランタイム | Python 3.12 |

## m5atom_power_adc 設計メモ

### U128（SIM7080G）電源デカップリング

- U128モジュール内部に5V側22μFが搭載済み
- 外付けコンデンサ推奨値: **47μF + 100nF**（コネクタ端、X5R/X7R、16V以上）
- 100nFは50V品でも問題なし

### 電流測定IC: LTC2944 → INA228 代替

| 項目 | LTC2944 (C683672) | INA228 (C2862904) |
| ---- | ----------------- | ----------------- |
| パッケージ | DFN-8 (2×3mm) | VSSOP-10 |
| 自己電源 | SENSE+から自己給電（3.6〜60V） | VSピン必要（2.7〜5.5V） |
| 高圧側耐圧 | 60V | 85V |
| ADC | 16-bit | 20-bit |
| クーロンカウンタ | 16-bit | 40-bit |
| 電荷精度 | ±0.6% | ±1.0% FSR (max) |
| 電流精度 | ±0.4% | ±0.05% (max) |
| 温度測定 | あり（±3°C） | あり（±1°C） |
| I2Cアドレス | 固定 0x64 | 16通り（0x40〜0x4F） |
| LCSC価格 | ~$12.4 | ~$3.0 |

**移行時の変更点:**

- フットプリント変更必須（DFN-8 → VSSOP-10）
- VSピンに3.3V供給が必要
- I2Cアドレス: 0x64 → 0x40（A0/A1=GND）※ 競合なし
- レジスタマップが異なる → ドライバ要書き直し

**INA228 注意事項（データシートより）:**

- 起動時に`SHUNT_CAL`レジスタを毎回書き込む必要あり（VSリセットで消える）
- ADCRANGE=1使用時は`SHUNT_CAL`の計算値を×4する
- VSピン直近に100nFデカップリング必須
- IN+/IN-はケルビン接続推奨
- A0/A1はフローティング禁止（GND/SCL/SDA/VSのいずれかに接続）
- VBUSはIN+と同ノードに接続（PCB上で直結可）

### シャント抵抗選定（電流測定）

- バッテリー: LiFePO4 100Ah × 2並列
- 配線: 14sq → 許容電流 約80A（ボトルネック）
- シャント抵抗は**コネクタ経由で外付け**
- **ADCRANGE=1**（±40.96mV）採用
- 推奨シャント: **200A 75mV品**（R = 0.375mΩ）
  - フルスケール電流: ±109A
  - 80A時使用率: 73%
  - 電流LSB: 208μA
- ログフォーマット: `x.xxA`（%.2f）
- 実用精度下限: 10mA以上

**校正データ（2点校正 + 温度補正）:**

```text
実電流 = (ADC読み値 - オフセット) × ゲイン係数
```

| データ | 取得条件 |
| ------ | -------- |
| オフセット電流値 | 電流ゼロ、各温度 |
| ゲイン係数 | 既知電流（複数点推奨）、各温度 |
| シャント抵抗の実測値 | 25°C基準 |

INA228は温度センサー内蔵なので測定時の温度を同時取得可能。

---

## 将来の拡張候補

- **BLE スキャン**: SwitchBot 温湿度計 / CO2 センサーを esp32_iot_gateway に追加（m5atom 版の実装は参考にできる）
- **コマンド受信**: AWS IoT Core から MQTT でリレー制御・設定変更を受信
- **Web 管理画面**: デバイス設定のリモート管理
- **DDoS・コスト対策強化**（不特定多数公開時）:
  - API も CloudFront 経由に統一し、API Gateway に Resource Policy で直接アクセスを禁止
  - CloudFront に AWS WAF を付けてレートリミットを適用（$5/月〜）
  - 現状は API GW スロットリングのみ適用（個人用途では十分）
