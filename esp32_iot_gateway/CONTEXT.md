# プロジェクトコンテキスト

## 概要

車載 IoT システム。ESP32-S3-MINI-1 が ADS1115 でサブ/メイン 2 系統のバッテリー電圧を測定し、
INA228 でサブバッテリーの電流・電力・積算電力量を測定し、
SwitchBot BLE センサー（WoIOSensor 防水温湿度計 / CO2センサー）の BLE アドバタイズをスキャンして、
SIM7080G（SORACOM Cat-M）経由で AWS IoT Core に MQTT over TLS で送信する。

クラウド側は m5atom_iot_gateway と共通（S3 + Athena + API Gateway + Lambda + CloudFront）。

## ハードウェア

| 項目 | 内容 |
| ---- | ---- |
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

### 起動フロー（共通）

1. 周辺機器初期化（OLED / ADS / INA228 / スピーカー / ボタン / BLE スキャナー）
2. DeepSleep 復帰かどうかを `esp_sleep_get_wakeup_cause()` で判定（復帰時はメロディをスキップ）
3. BTN0 を押しながら起動 → `enterMenuMode()` → `OperationMode` を返す（LTE 未起動のままオフライン動作）
4. LTE 接続 → OTA チェック → `loop()` へ

### DEEP_SLEEP モード（`#define DEBUG_MODE` コメントアウト時のデフォルト）

`loop()` が1サイクルで完結し DeepSleep に入る：

1. `measure()`: BLE スキャン（10秒）+ ADS1115/INA228 読み取り → `MeasureResult` を返す
2. `publish()`: Shadow + BLE センサーペイロードを MQTT publish
3. OLED に計測値を表示
4. LTE 切断 → `lte.radioOff()`（LTE_EN=LOW）→ DeepSleep 5分 → `setup()` から再起動

### CONTINUOUS モード（`#define DEBUG_MODE` 有効時のデフォルト、またはメニューで選択）

`loop()` が繰り返す：

1. `measure()` → `publish()` → OLED 表示（DEEP_SLEEP モードと同じ計測・送信処理）
2. SLEEP_INTERVAL_SEC（300秒）待機しながらボタン監視・カウントダウン表示
   - BTN0 短押し: メニューを開く（終了後 OLED を計測値画面に復元）
   - BTN1 長押し: DEEP_SLEEP モードへ切り替え（次ループで DeepSleep）

### メニューモード（起動時 BTN0 長押し、または CONTINUOUS 待機中 BTN0 短押し）

OLED + 2ボタンの設定メニュー。詳細は `MENU.md` 参照。

- `BLE: Register` — BLE スキャンして SwitchBot デバイスを NVS 登録
- `BLE: Remove` — 登録済みデバイスを NVS から削除
- `Sensor View` — ADS1115/INA228 リアルタイム表示（50ms 更新）
- `System Info` — FW バージョン・デバイス ID 表示
- `Continuous` — CONTINUOUS モードで起動（LTE を開始して loop へ）
- `Restart` — `esp_restart()`

## ファイル構成

3層アーキテクチャ（device / domain / service）。詳細は `ARCHITECTURE.md` 参照。

```text
esp32_iot_gateway/
├── platformio.ini                     PlatformIO ビルド設定
├── extra_scripts.py                   ビルド前フック: git hash を GIT_HASH マクロとして注入
├── CONTEXT.md                         本ファイル
├── ARCHITECTURE.md                    レイヤー構成・依存ルール・命名規則
└── src/
    ├── main.cpp                       エントリポイント: 初期化・起動モード判定・loop() で計測/送信サイクル
    ├── config.h / config.cpp          全層共通定数・NVS アクセス（デバイスID / MQTT ホスト / 証明書 CRC / OTA ジョブID）
    ├── provision.cpp                  プロビジョニング専用（provision env のみビルド）
    ├── device/
    │   ├── lte.h/.cpp                 SIM7080G ATコマンド制御（GPRS接続・証明書アップロード・電源管理・ファイル読み取り・削除）
    │   ├── ble_scan.h/.cpp            BLE スキャナー（SwitchBot デバイス受信・FreeRTOS キュー）
    │   ├── ads.h/.cpp                 ADS1115 I2Cドライバ（差動電圧読み取り）
    │   ├── ina228.h/.cpp              INA228 I2Cドライバ（電流・電力・温度読み取り）
    │   ├── oled.h/.cpp                SSD1306 OLEDドライバ（表示制御）
    │   ├── speaker.h/.cpp             ブザードライバ（tone PWM制御・ノンブロッキング playTone() 含む）
    │   └── button.h/.cpp              デバウンス・長押し検出（BTN0/BTN1 ピン定数内包、フィードバック音内蔵）
    ├── domain/
    │   ├── measurement.h              計測値構造体（VoltageReading, PowerReading）
    │   ├── telemetry.h/.cpp           ペイロード JSON 組み立て（Shadow / Thermometer / CO2）
    │   ├── sensor.h                   BLE センサー共通構造体（SensorBase）
    │   ├── thermometer.h/.cpp         SwitchBot 温湿度計パーサー
    │   ├── co2meter.h/.cpp            SwitchBot CO2センサーパーサー
    │   ├── sensor_factory.h/.cpp      センサー種別振り分け（SensorVariant）
    │   └── ble_targets.h/.cpp         監視対象 BLE アドレスの NVS 永続化
    └── service/
        ├── mqtt.h/.cpp                MQTT publish / subscribe / pollMqtt（device/lte をトランスポートとして使用）
        ├── https.h/.cpp               HTTPS GET（AT+SH*）/ ファイルダウンロード（AT+HTTPTOFS）
        ├── ota.h/.cpp                 AWS IoT Jobs 確認・ファームウェア適用・ロールバック管理
        ├── monitor.h/.cpp             計測サイクル（measure() / publish()）・MeasureResult 定義
        ├── menu.h/.cpp                OLED + 2ボタン設定メニュー（enterMenuMode() → OperationMode）
        └── logger.h/.cpp              シリアルデバッグ出力
└── lib/
    └── uzlib/                         gzip 解凍ライブラリ（esp32-targz 同梱版・gzip OTA 圧縮実装準備）
```

## データフロー

m5atom_iot_gateway と共通（AWS IoT Core → S3 → Athena）。

```text
ESP32-S3-MINI-1
  → MQTT over TLS（SIM7080G）
  → AWS IoT Core  topic: sensors/{device_id}/data
  → Topic Rule SQL: SELECT *, topic(2) AS device_id FROM 'sensors/+/data'
  → Lambda ingest
  → S3: raw/year=YYYY/month=MM/day=DD/hour=HH/{device_id}-{uuid8}.json
```

## MQTT ペイロード形式

### デバイスシャドウ更新

トピック: `$aws/things/{device_id}/shadow/update`

```json
{"state":{"reported":{"v1":12.34,"v2":12.10,"current":5.2100,"power":62.500,"temp":28.5,"ts":1746143400}}}
```

| フィールド | 型 | 内容 |
| --- | --- | --- |
| `v1` | float | サブバッテリー電圧（V）、ADS1115 ch0（AIN0-AIN1 差動） |
| `v2` | float | メインバッテリー電圧（V）、ADS1115 ch1（AIN2-AIN3 差動） |
| `current` | float | サブバッテリー電流（A）、INA228 |
| `power` | float | サブバッテリー電力（W）、INA228 |
| `temp` | float | INA228 内蔵温度センサー（°C） |
| `ts` | int | UNIX タイムスタンプ（秒） |

### OTA / Jobs トピック（service/ota が使用）

| トピック | 方向 | 用途 |
| --- | --- | --- |
| `$aws/things/{id}/jobs/$next/get` | publish | 次ジョブ取得リクエスト |
| `$aws/things/{id}/jobs/$next/get/accepted` | subscribe | ジョブ取得レスポンス |
| `$aws/things/{id}/jobs/{jobId}/update` | publish | ジョブ状態更新（IN_PROGRESS / SUCCEEDED / FAILED） |

## 主要クラスとインスタンス

| インスタンス | 型 | 役割 |
| --- | --- | --- |
| `lte` | `Lte` | SIM7080G ATコマンド制御・GPRS接続・証明書管理・ファイル読み取り・削除 |
| `mqtt` | `Mqtt` | MQTT publish / subscribe / pollMqtt |
| `https` | `Https` | AT+SH* 経由 HTTPS GET / ダウンロード |
| `ota` | `Ota` | AWS IoT Jobs 確認・FW 適用・ロールバック管理 |
| `bleScanner` | `BleScanner` | BLE スキャン・FreeRTOS キューへの書き込み |
| `bleTargets` | `BleTargets` | 監視対象 BLE アドレスの NVS 管理 |
| `button` | `Button` | BTN0/BTN1 デバウンス・長押し検出（`ButtonEvent`）・フィードバック音 |
| `logger` | `Logger` | Serial デバッグ出力（`printf`/`println`） |

ドメイン型:

| 型 | 役割 |
| --- | --- |
| `VoltageReading` | 電圧計測値（`float voltage`） |
| `PowerReading` | 電力計測値（`float current, power, temp`） |
| `SensorReading` | アナログ計測値まとめ（`VoltageReading v1, v2` + `PowerReading pwr` + `time_t ts`） |
| `MeasureResult` | 1サイクルの全計測結果（`SensorReading reading` + `SensorVariant ble[QUEUE_SIZE]` + `int bleCount`） |
| `ThermometerData` | SwitchBot 温湿度計データ（SensorBase + temp/humidity/battery） |
| `Co2MeterData` | SwitchBot CO2センサーデータ（ThermometerData + co2） |
| `SensorVariant` | `std::variant<ThermometerData, Co2MeterData>` |

## 重要な設計決定

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

詳細（部品比較・シャント設計）: `m5atom_power_adc/HARDWARE.md` の「サブバッテリー電流計測（INA228）」セクション参照。

### OLED ディスプレイ

`device/oled.h/.cpp` に SSD1306 ドライバを実装済み（I2C アドレス `0x3C`、SDA=GPIO17, SCL=GPIO18）。

| 関数 | 用途 |
| --- | --- |
| `oledInit()` | I2C 初期化・画面クリア |
| `oledPrint(text)` | 1行テキスト表示 |
| `oledShowSensorData(SensorReading&)` | 計測値全表示（v1/v2/電流/電力/温度） |
| `oledUpdateCountdown(remainSec)` | 計測値画面下部のカウントダウン行のみ部分更新（CONTINUOUS モード用） |
| `oledShowMessage(line1, line2)` | 2行メッセージ表示 |
| `oledShowMenu(title, items, count, cursor)` | スクロール付きメニュー表示 |
| `oledShowConfirm(message, item, cursor)` | Yes/No 確認ダイアログ |
| `oledShowOtaProgress(stage, current, total)` | OTA 進捗バー表示 |

### ADS1115 2 チャンネル読み取り

ch0 = `readADC_Differential_0_1()`（サブバッテリー）、ch1 = `readADC_Differential_2_3()`（メインバッテリー）。
チャンネル切り替え後、変換完了を待ってから読む（`readADC_Differential_X_Y()` はシングルショットのため自動待機）。

分圧比: `22 / (680 + 22) ≒ 0.03134`。変換式: `V = -adcV / DIV_RATIO`（差動測定のため符号反転）。

### LTE 電源スイッチ（LTE_EN）

`setup()` で `LTE_EN=HIGH`（GPIO9）にしてから SIM7080G を起動する。
DeepSleep 前に MQTT/GPRS 切断 → `lte.radioOff()` → `LTE_EN=LOW` の順で電源を落とす。
スリープ中の SIM7080G 消費電流を遮断できる。

### NVS 永続化

`nvs.h` を直接使用。namespace は用途ごとに分離:

| namespace | 用途 |
| --- | --- |
| `"device"` | MQTT ホスト（`mqtt_host`） |
| `"lte"` | 証明書 CRC（`cert_crc`） |
| `"ota"` | 保留中 OTA ジョブ ID（`job_id`） |

### LTE / MQTT（SIM7080G + AWS IoT Core）

m5atom_iot_gateway と同一設計。以下の注意事項も継承:

- `modem.restart()` は使わず `modem.init()` を使う
- APN 設定は `CFUN=0 → CGDCONT → CFUN=1` の順（必須）
- DeepSleep 前に必ず `lte.disconnect()` → `lte.radioOff()` を呼ぶ

## config.h 定数一覧

| 定数 | 値 | 用途 |
| --- | --- | --- |
| `FIRMWARE_VERSION` | `"1.3.0+" GIT_HASH` | ファームウェアバージョン |
| `GIT_HASH` | ビルド時注入（8文字 hex） | `extra_scripts.py` が `-DGIT_HASH` で定義 |
| `OperationMode` | enum class | `DEEP_SLEEP` / `CONTINUOUS`（動作モード） |
| `SLEEP_INTERVAL_SEC` | `300` | DeepSleep 間隔 / CONTINUOUS モード待機間隔（秒） |
| `CERT_PATH_CA` | `"/certs/ca.crt"` | SPIFFS 上の CA 証明書パス |
| `CERT_PATH_DEVICE` | `"/certs/device.crt"` | SPIFFS 上のデバイス証明書パス |
| `CERT_PATH_KEY` | `"/certs/device.key"` | SPIFFS 上の秘密鍵パス |
| `MQTT_PORT` | `8883` | AWS IoT Core MQTT ポート |
| `SWITCHBOT_COMPANY_ID` | `0x0969` | SwitchBot BLE Manufacturer ID |
| `SCAN_TIME` | `10` | BLE スキャン時間（秒） |
| `QUEUE_SIZE` | `20` | BLE キュー最大サイズ |
| `MAX_TARGETS` | `10` | 監視対象 BLE デバイス最大数 |
| `PAYLOAD_SENSOR_SIZE` | `256` | BLE センサーペイロードバッファ（バイト） |

device/lte.h の定数:

| 定数 | 値 | 用途 |
| --- | --- | --- |
| `LTE_RX_PIN` | `7` | GPIO7 ← U128 TXD |
| `LTE_TX_PIN` | `8` | GPIO8 → U128 RXD |
| `LTE_EN_PIN` | `9` | GPIO9（AO3401A パワースイッチ制御） |
| `APN` | `"soracom.io"` | SORACOM APN |
| `APN_USER` | `"sora"` | SORACOM APN ユーザー |
| `APN_PASS` | `"sora"` | SORACOM APN パスワード |
| `SEND_INTERVAL_SEC` | `60` | デバッグモード送信間隔（秒） |

## ビルド環境

| 項目 | 内容 |
| ---- | ---- |
| IDE | PlatformIO |
| プラットフォーム | espressif32 |
| ボード | esp32-s3-devkitc-1（ESP32-S3-MINI-1 互換） |
| C++ 標準 | C++17（`-std=gnu++17`） |
| ビルドフック | `extra_scripts.py`（`pre:`）— git hash を `GIT_HASH` マクロとして注入 |
| 主要ライブラリ | TinyGSM, ArduinoJson, Adafruit SSD1306, Adafruit GFX, Adafruit ADS1X15 |

## 作業中・引き継ぎ事項

### TODO: OTA ファームウェアの gzip 圧縮（任意・高速化）

**背景**: 実測で Phase1（セルラー DL）71秒・Phase2（UART 読み取り）127秒。Phase2 がボトルネック。

**方針**:

- `deploy_ota.ps1` でビルド後に `firmware.bin.gz` として S3 にアップロード
- `ota.apply()` 内で `lte.readFile()` コールバックに uzlib のストリーミング解凍を挟む
- `uzlib` は `lib/uzlib/` に配置済み（esp32-targz 同梱版）
- 解凍 API: `uzlib_uncompress_init()` → `uzlib_gzip_parse_header()` → `uzlib_uncompress_chksum()`
- 32KB のスライディングウィンドウバッファが必要（ESP32 の RAM には余裕あり）

**期待効果**: gzip 50% 圧縮で Phase1 ~35秒・Phase2 ~60秒 → 合計 ~97秒（現状 198秒）

### フェーズ 2: AWS IoT からのコマンド受信（未着手）

| 機能 | 概要 |
| ---- | ---- |
| Subscribe トピック | `sensors/{device_id}/command` |
| 想定コマンド | リレー制御、設定変更、即時送信トリガー等 |

### ~~フェーズ 4: 操作ボタン UI~~ **実装済み**

OLED + 2ボタンの設定メニューを実装。詳細は `MENU.md` 参照。

- `service/menu.h/.cpp` — メニューステートマシン本体（`enterMenuMode()` → `OperationMode` を返す）
- `service/monitor.h/.cpp` — 計測サイクル（`measure()` / `publish()`）
- `device/button.h/.cpp` — デバウンス・長押し検出（BTN0/BTN1 ピン定数内包、フィードバック音内蔵）
- BTN0（GPIO26）: カーソル移動 / CONTINUOUS 待機中はメニュー呼び出し
- BTN1（GPIO33）: 決定 / 長押しで戻る / CONTINUOUS 待機中は DEEP_SLEEP↔CONTINUOUS トグル
- 起動時に BTN0 を押しながら電源 ON でメニューモードに入る（LTE 未起動）
- CONTINUOUS 待機中に BTN0 短押しでもメニューを呼び出せる（LTE 接続済みのまま）

### TODO: INA228 設定メニューの追加（未着手）

INA228 ドライバを `Ina228` クラスに移行済み（`device/ina228.h/.cpp`）。
今後の機能拡張に備えて、OLED + 2ボタンメニューから INA228 の設定変更を行えるようにする。

**想定する設定項目（未確定）**:

- SHUNT_CAL の手動調整（シャント抵抗値の校正）
- 平均化サンプル数の変更（AVG: 1/4/16/64/128/256/512/1024）
- 変換時間の変更（VBUSCT / VSHCT）
- 積算電荷量のリセット（`ina228.resetCharge()` は現状 `setup()` で自動実行）

**実装方針**:

- `service/menu.cpp` の `MenuState` に `INA228_SETTINGS` を追加
- メインメニューに "INA228 Config" 項目を追加
- 設定値は NVS に永続化する（`config.cpp` の `nvs` 操作パターンに倣う）

### フェーズ 5: リレー・ブザー制御（未着手）

| 機能 | GPIO | 概要 |
| ---- | ---- | ---- |
| リレー制御 × 3 | IO11/IO13/IO15 | NPN BJT（MMBT2222A）ドライバ、HIGH = ON |
| リレーセンシング × 3 | IO10/IO12/IO14 | 外部スイッチ検出、負論理（HIGH = OFF） |
| ブザー | IO35 | AO3401A ハイサイドスイッチ、LEDC PWM 2700Hz、負論理 |
