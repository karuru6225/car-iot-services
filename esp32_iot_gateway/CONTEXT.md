# プロジェクトコンテキスト

## 概要

車載 IoT システム。ESP32-S3-MINI-1 が ADS1115 でサブ/メイン 2 系統のバッテリー電圧を測定し、
INA228 でサブバッテリーの電流・電力・積算電力量を測定し、
SwitchBot BLE センサー（WoIOSensor 防水温湿度計 / CO2センサー）の BLE アドバタイズをスキャンして、
SIM7080G（SORACOM Cat-M）経由で AWS IoT Core に MQTT over TLS で送信する。

クラウド側は m5atom_iot_gateway と共通（S3 + Athena + API Gateway + Lambda + CloudFront）。

## ハードウェア

| 項目 | 内容 |
|---|---|
| MCU | ESP32-S3-MINI-1-N8（KiCad プロジェクト `m5atom_power_adc` 基板直付け） |
| ディスプレイ | なし |
| ボタン | GPIO26（Btn0）/ GPIO33（Btn1）（Active-LOW、外部プルアップ） |
| ADC | ADS1115（I2C: SDA=GPIO17, SCL=GPIO18, ADDR=0x48） |
| 電圧測定回路 ch0 | 差動入力（AIN0-AIN1）、分圧回路 R_upper=680kΩ / R_lower=22kΩ、GAIN_EIGHT(±0.512V)、**サブバッテリー** |
| 電圧測定回路 ch1 | 差動入力（AIN2-AIN3）、分圧回路 R_upper=680kΩ / R_lower=22kΩ、GAIN_EIGHT(±0.512V)、**メインバッテリー** |
| ADC_READY | GPIO16（ADS1115 ALERT/RDY、変換完了割り込み） |
| 電流計 | INA228（I2C: SDA=GPIO17, SCL=GPIO18, ADDR=0x40、A0/A1=GND） |
| 電流計対象 | サブバッテリー電流・電力・積算電力量・温度 |
| 電流計シャント | コネクタ経由外付け、200A 75mV 品（R=0.375mΩ）、ADCRANGE=1（±40.96mV） |
| OLED | I2C 接続予定（コネクタ経由、SDA=GPIO17, SCL=GPIO18）※未実装 |
| センサー | SwitchBot WoIOSensor（温湿度計）/ CO2センサー（BLE アドバタイズのみ、接続不要） |
| LTE | M5Stack U128（SIM7080G CAT-M/NB-IoT）、SORACOM SIM |
| LTE ピン | RX=GPIO4 ← U128 TXD、TX=GPIO5 → U128 RXD |
| LTE 電源 | GPIO6（LTE_EN: HIGH=ON、AO3401A パワースイッチ経由） |
| 電源 | 車載 12V バッテリー → LM2596（12V→5V）→ AMS1117-3.3（5V→3.3V）→ ESP32 |

> **参照**: 回路詳細は `m5atom_power_adc/CIRCUIT.md`、部品・GPIO設計は `HARDWARE.md` を参照。

## 動作モード

### 本番モード（デフォルト、`#define DEBUG_MODE` コメントアウト時）

1. BLE スキャン（10 秒）
2. ADS1115 から 2 系統電圧読み取り + INA228 から電流・電力・積算電力量読み取り + キューから BLE データを収集してペイロード生成
3. LTE 接続確認・再接続（切断時）
4. 全ペイロードを AWS IoT Core に MQTT publish（最大 3 分リトライ）
5. MQTT/GPRS 切断 → LTE 電源オフ（LTE_EN=LOW）→ DeepSleep 5 分 → `setup()` から再起動

### デバッグモード（`#define DEBUG_MODE` 有効時）

1. BLE スキャン → 電圧測定 → MQTT publish → SEND_INTERVAL_SEC（60 秒）待機
2. 待機中に長押し検出で登録モードへ
3. DeepSleep しない（繰り返しループ）

### 登録モード（デバッグモード中に長押しで起動）

1. BLE スキャンで SwitchBot デバイスを検索
2. 見つかったデバイス一覧を Serial 出力
3. 短押し（Btn0）: 次の項目へ
4. 長押し（Btn0）: 選択中のデバイスを登録（未登録）または削除（登録済）、または戻る

## ファイル構成

```
esp32_iot_gateway/
├── platformio.ini                     PlatformIO ビルド設定
├── extra_scripts.py                   ビルド前フック: git hash を GIT_HASH マクロとして注入
├── CONTEXT.md                         本ファイル
└── src/
    ├── main.cpp                       Application: メインループ・モード管理
    ├── config.h                       定数（スキャン時間・ピン番号・閾値等）
    ├── certs.h                        AWS IoT Core 証明書・秘密鍵（gitignore）
    ├── certs.example.h                証明書のサンプルテンプレート
    ├── domain/
    │   ├── sensor.h                   Value Object 基底: SensorBase 構造体
    │   ├── thermometer.h/.cpp         Value Object: 温湿度計データ・パース
    │   ├── co2meter.h/.cpp            Value Object: CO2センサーデータ・パース
    │   ├── sensor_factory.h/.cpp      Factory: センサー種別判定・パーサー振り分け
    │   └── targets.h/.cpp             Aggregate + Repository: 監視対象リスト
    ├── infra/
    │   ├── ble_scan.h/.cpp            Infrastructure: BLE スキャン
    │   ├── button.h/.cpp              Infrastructure: GPIO ボタン（Btn0/Btn1 2ボタン）
    │   ├── ina228.h/.cpp              Infrastructure: INA228 電流・電力・積算電力量測定
    │   ├── lte.h/.cpp                 Infrastructure: SIM7080G MQTT over TLS + LTE_EN 制御
    │   └── logger.h/.cpp              Infrastructure: Serial デバッグ出力
    └── app/
        ├── view.h/.cpp                Presentation: Serial 出力（Display なし）
        ├── register_mode.h/.cpp       Application Service: 登録ユースケース
        └── bypass_mode.h/.cpp         AT コマンド透過モード（デバッグ用）
```

## データフロー

m5atom_iot_gateway と共通（AWS IoT Core → S3 → Athena）。

```
ESP32-S3-MINI-1
  → MQTT over TLS（SIM7080G）
  → AWS IoT Core  topic: sensors/{device_id}/data
  → Topic Rule SQL: SELECT *, topic(2) AS device_id FROM 'sensors/+/data'
  → Lambda ingest
  → S3: raw/year=YYYY/month=MM/day=DD/hour=HH/{device_id}-{uuid8}.json
```

## MQTT ペイロード形式

### バッテリー電圧（battery タイプ）

```json
{"type":"battery","id":"voltage_1","voltage":12.34,"fw":"1.0.0+a364343b","ts":"2026-03-05T12:00:00Z"}
{"type":"battery","id":"voltage_2","voltage":12.10,"fw":"1.0.0+a364343b","ts":"2026-03-05T12:00:00Z"}
```

`id` は `voltage_1`（サブバッテリー ch0）と `voltage_2`（メインバッテリー ch1）の 2 件を送信する。

### 温湿度（thermometer タイプ）

```json
{"type":"thermometer","addr":"AA:BB:CC:DD:EE:FF","temp":25.0,"humidity":60,"battery":80,"rssi":-70,"mf":"09b0e9...","fw":"1.0.0+a364343b","ts":"2026-03-05T12:00:00Z"}
```

### CO2（co2meter タイプ）

```json
{"type":"co2meter","addr":"AA:BB:CC:DD:EE:FF","temp":25.0,"humidity":60,"co2":612,"battery":80,"rssi":-70,"mf":"09b0e9...","fw":"1.0.0+a364343b","ts":"2026-03-05T12:00:00Z"}
```

### 電流計（current_meter タイプ）

```json
{"type":"current_meter","id":"sub_battery","current":12.34,"power":148.1,"energy":0.041,"temp":28.5,"fw":"1.0.0+a364343b","ts":"2026-03-05T12:00:00Z"}
```

| フィールド | 型 | 内容 |
|---|---|---|
| `id` | string | `"sub_battery"`（サブバッテリー固定） |
| `current` | float | 電流（A）、2桁小数 |
| `power` | float | 電力（W）、1桁小数 |
| `energy` | float | 積算電力量（kWh）、3桁小数 |
| `temp` | float | INA228 内蔵温度センサー（°C）、1桁小数 |

積算電力量（energy）は INA228 の 40bit ハードウェアアキュムレータを読む。DeepSleep を跨いでも INA228 の VS ピンに電源が供給され続ける限り積算が継続する。

## 主要クラスとインスタンス

| インスタンス | 型 | 役割 |
|---|---|---|
| `targets` | `Targets` | 監視対象 BLE アドレスの管理・NVS 読み書き |
| `scanner` | `BleScanner` | BLE スキャン・FreeRTOS キュー管理 |
| `button` | `Button` | ISR ベースのボタンイベント検出（Btn0/Btn1） |
| `ina228` | `Ina228` | INA228 電流・電力・積算電力量・温度読み取り |
| `regMode` | `RegisterMode` | 登録モードのロジック |
| `view` | `View` | Serial 出力（Display なし） |
| `lte` | `Lte` | SIM7080G 制御・MQTT over TLS publish・LTE_EN 制御 |
| `logger` | `Logger` | Serial デバッグ出力（`printf`/`println`） |

ドメイン型（m5atom_iot_gateway と同一）:

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

### M5Unified 不使用・View は Serial のみ

ディスプレイが搭載されていないため `M5Unified` は使用しない。
`View` クラスは Serial 出力のみ。`include` 順序の制約（m5atom 版では M5Unified を BLE より先に include する必要があった）も不要になる。

### BLE コールバックと FreeRTOS キュー（std::variant）

m5atom_iot_gateway と同一設計。
`SwitchBotCallback::onResult()` は BLE タスクから呼ばれ、`SensorVariant`（`std::variant<ThermometerData, Co2MeterData>`）
に格納して `xQueueSend(scanner.queue, &v, 0)` でメインタスクに渡す。

FreeRTOS キューは `memcopy` ベースのため virtual 関数（vtable/vptr）を持つ型は使用不可。
メインタスクでは `std::visit` + `if constexpr` で型ごとに処理を分岐する。

### INA228 設計ノート

INA228 は VSSOP-10 パッケージ、I2C アドレス `0x40`（A0/A1=GND）。
ADS1115 と同じ I2C バス（SDA=GPIO17, SCL=GPIO18）に並列接続。

**起動時の初期化手順（毎回必須）：**

1. `SHUNT_CAL` レジスタを書き込む（VS リセットで消えるため DeepSleep 復帰後も必要）
2. ADCRANGE=1（`CONFIG` レジスタ bit2=1）を設定する

**SHUNT_CAL 計算（ADCRANGE=1）：**

```text
SHUNT_CAL = 819.2 × 10^6 × CURRENT_LSB × R_shunt × 4  ← ADCRANGE=1 なので×4
シャント: 0.375mΩ（200A/75mV品）、フルスケール電流 ±109A
CURRENT_LSB = 109A / 2^19 ≒ 208μA
SHUNT_CAL = 819.2e6 × 208e-6 × 0.375e-3 × 4 ≒ 255
```

**注意事項：**

- A0/A1 はフローティング禁止（GND/SCL/SDA/VS のいずれかに接続）
- VS ピン直近に 100nF デカップリング必須
- VBUSはIN+と同ノードに接続（PCB上で直結）
- 起動時に SHUNT_CAL を毎回書き込む（電源 OFF でリセット）
- ADCRANGE=1 使用時は SHUNT_CAL の値を×4すること（上記計算に含み済み）

詳細: `CONTEXT.md`（プロジェクトルート）の「電流測定IC: LTC2944 → INA228 代替」セクション参照。

### OLED ディスプレイ（将来）

コネクタ経由で同一 I2C バスに接続予定。アドレスは一般的な SSD1306 の場合 `0x3C`。
現フェーズでは未実装。実装時は `app/view.h` に Display 出力を追加する。

### ADS1115 2 チャンネル読み取り

ch0 = `readADC_Differential_0_1()`（サブバッテリー）、ch1 = `readADC_Differential_2_3()`（メインバッテリー）。
チャンネル切り替え後、変換完了を待ってから読む（`readADC_Differential_X_Y()` はシングルショットのため自動待機）。

分圧比: `22 / (680 + 22) ≒ 0.03134`。変換式: `V = -adcV / DIV_RATIO`（差動測定のため符号反転）。

### LTE 電源スイッチ（LTE_EN）

`setup()` で `LTE_EN=HIGH`（GPIO6）にしてから SIM7080G を起動する。
DeepSleep 前に MQTT/GPRS 切断 → `lte.radioOff()` → `LTE_EN=LOW` の順で電源を落とす。
スリープ中の SIM7080G 消費電流を遮断できる。

### ボタン ISR

`btnISR()` は `IRAM_ATTR` 付きの静的関数（Arduino の制約でメンバ関数にできない）。
ファイルスコープの `static volatile bool sLongFired / sShortFired` で状態を持つ。
Btn0（GPIO26）を主操作ボタンとして使用。Btn1（GPIO33）は将来用途向けに確保。

### NVS 永続化

m5atom_iot_gateway と同一。`Preferences` ライブラリで namespace `"switchbot"` に保存。

### LTE / MQTT（SIM7080G + AWS IoT Core）

m5atom_iot_gateway と同一設計。以下の注意事項も継承:

- `modem.restart()` は使わず `modem.init()` を使う
- APN 設定は `CFUN=0 → CGDCONT → CFUN=1` の順（必須）
- DeepSleep 前に必ず `lte.disconnect()` → `lte.radioOff()` を呼ぶ

## config.h 定数一覧

| 定数 | 値 | 用途 |
| --- | --- | --- |
| `FIRMWARE_VERSION` | `"1.0.0+" GIT_HASH` | ファームウェアバージョン |
| `GIT_HASH` | ビルド時注入（8文字 hex） | `extra_scripts.py` が `-DGIT_HASH` で定義 |
| `SWITCHBOT_COMPANY_ID` | `0x0969` | BLE フィルタリング |
| `SCAN_TIME` | `10` | BLE スキャン秒数 |
| `QUEUE_SIZE` | `20` | FreeRTOS キューサイズ |
| `MAX_TARGETS` | `10` | 登録可能デバイス上限 |
| `MAX_FOUND` | `20` | スキャンで見つかるデバイス上限 |
| `BTN0_PIN` | `26` | Btn0 GPIO（主操作ボタン） |
| `BTN1_PIN` | `33` | Btn1 GPIO（予備） |
| `LONG_PRESS_MS` | `1000` | 長押し判定ミリ秒 |
| `DEBOUNCE_MS` | `50` | チャタリング除去ミリ秒 |
| `SLEEP_INTERVAL_SEC` | `300` | Deep Sleep 間隔（秒） |
| `PAYLOAD_BATTERY_SIZE` | `128` | battery ペイロードバッファ |
| `PAYLOAD_SENSOR_SIZE` | `256` | sensor ペイロードバッファ（mf hex 込み） |
| `ADS_I2C_ADDR` | `0x48` | ADS1115 I2C アドレス（ADDR→GND） |
| `INA_I2C_ADDR` | `0x40` | INA228 I2C アドレス（A0/A1=GND） |
| `ADS_SDA_PIN` | `17` | I2C SDA ピン（ADS1115 / INA228 / OLED 共通） |
| `ADS_SCL_PIN` | `18` | I2C SCL ピン（ADS1115 / INA228 / OLED 共通） |
| `INA_SHUNT_CAL` | `255` | INA228 SHUNT_CAL 値（ADCRANGE=1、シャント 0.375mΩ） |
| `INA_CURRENT_LSB` | `208e-6` | INA228 電流 LSB（A/bit） |

infra/lte.h の定数:

| 定数 | 値 | 用途 |
| --- | --- | --- |
| `LTE_RX_PIN` | `4` | GPIO4 ← U128 TXD |
| `LTE_TX_PIN` | `5` | GPIO5 → U128 RXD |
| `LTE_EN_PIN` | `6` | GPIO6（AO3401A パワースイッチ制御） |
| `APN` | `"soracom.io"` | SORACOM APN |
| `SEND_INTERVAL_SEC` | `60` | デバッグモード送信間隔（秒） |

## ビルド環境

| 項目 | 内容 |
|---|---|
| IDE | PlatformIO |
| プラットフォーム | espressif32 |
| ボード | esp32-s3-devkitc-1（ESP32-S3-MINI-1 互換） |
| C++ 標準 | C++17（`-std=gnu++17`）※ `std::variant` に必須 |
| ビルドフック | `extra_scripts.py`（`pre:`）— git hash を `GIT_HASH` マクロとして注入 |
| 主要ライブラリ | ESP32 BLE Arduino, Preferences, Adafruit ADS1X15, TinyGSM |
| BLE API | `getManufacturerData()` / `getServiceData()` が `std::string` を返す |

## 作業中・引き継ぎ事項

### フェーズ 1（最優先）: OTA ファームウェアアップデート

HTTP OTA + S3（既存インフラ流用）。MQTT 経由で更新通知を受け取り、S3 の署名付き URL から FW をダウンロードして書き込む。AWS IoT Jobs を使えばデバイス管理（進捗・エラー記録）まで対応できる。

| 項目 | 内容 |
| ---- | ---- |
| 方式 | AWS IoT MQTT → ESP32 受信 → S3 署名付き URL → `esp_https_ota()` |
| パーティション | `partitions_two_ota.csv`（`app0` / `app1` 各 ~1.8MB）、8MB フラッシュなら余裕あり |
| OTA コード配置 | factory パーティション不要。OTA コードを通常の APP に同梱し `app0` / `app1` を交互に使う |
| デバイス管理 | AWS IoT Jobs を使うとデバイス側の進捗・エラー記録まで対応可能 |
| `platformio.ini` | `board_build.partitions = partitions_two_ota.csv` を追加 |

**実装フロー：**

1. `esp_ota_get_next_update_partition()` — 書き込み先パーティション（自分と逆側の app）を取得
2. `esp_ota_begin()` — 書き込み開始
3. `esp_ota_write()` — チャンクごとに書き込み（HTTP レスポンスをストリーミング）
4. `esp_ota_end()` — SHA-256 検証を自動実施（失敗すると `ESP_ERR_OTA_VALIDATE_FAILED` を返す）
5. `esp_ota_set_boot_partition()` — 次回起動先を新 FW に設定
6. `esp_restart()` — 再起動

- **ロールバック防止**: 起動後に正常動作を確認したら `esp_ota_mark_app_valid_cancel_rollback()` を呼ぶ。呼ばずに再起動すると bootloader が旧 FW に戻す（`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` 有効時）。
- **ダウンロード完了後・再起動前の検証**:
  - `esp_ota_end()` 内で SHA-256 検証は自動実施される。
  - さらに自前で確認したい場合は `esp_partition_mmap()` で書き込んだパーティションをメモリマップし、ptr から SHA-256 を自前計算して期待値と比較できる:

   ```c
   esp_ota_end(handle);  // ← ここで SHA-256 検証済み

   // さらに自前で確認したい場合
   const esp_partition_t *next = esp_ota_get_next_update_partition(NULL);
   esp_partition_mmap_handle_t mmap;
   const void *ptr;
   esp_partition_mmap(next, 0, size, ESP_PARTITION_MMAP_DATA, &ptr, &mmap);
   // ptr から SHA-256 を自前計算して期待値と比較
   ```

### フェーズ 2（次）: AWS IoT からのコマンド受信

AWS IoT Core から ESP32 へ MQTT でコマンドを送り、デバイスが応答する機能。

| 機能 | 概要 |
| ---- | ---- |
| Subscribe トピック | `sensors/{device_id}/command` |
| 想定コマンド | リレー制御、設定変更、即時送信トリガー等（詳細は実装時に確定） |
| 実装方針 | SIM7080G 内蔵 MQTT の AT+SMCONF+AT+SMSUB を使用、受信コールバックで解析 |

### フェーズ 3（次）: ディスプレイと操作ボタン

| 機能 | 概要 |
| ---- | ---- |
| OLED ディスプレイ | I2C コネクタ経由接続（SSD1306 想定、0x3C）、`app/view.h` に Display 出力を追加 |
| ボタン UI | Btn0（GPIO26）/ Btn1（GPIO33）で画面操作・登録モード遷移 |

### フェーズ 4（次）: 基本計測機能

- BLE スキャン（温湿度 / CO2）
- ADS1115 2 系統電圧測定（サブ / メイン）
- INA228 電流・電力・積算電力量・温度測定（サブバッテリー）
- SIM7080G MQTT over TLS
- 登録モード
- DeepSleep

### フェーズ 5（残り）: リレー・ブザー制御

| 機能 | GPIO | 概要 |
| ---- | ---- | ---- |
| リレー制御 × 3 | IO11/IO13/IO15 | NPN BJT（MMBT2222A）ドライバ、HIGH = ON |
| リレーセンシング × 3 | IO10/IO12/IO14 | 外部スイッチ検出、負論理（HIGH = OFF） |
| ブザー | IO35 | AO3401A ハイサイドスイッチ、LEDC PWM 2700Hz、負論理 |

### CO2センサー クラウド側対応（未実装）

m5atom_iot_gateway と同じ残作業。デバイス側は実装済みだがクラウド側（Lambda/S3/Web）は未対応。
