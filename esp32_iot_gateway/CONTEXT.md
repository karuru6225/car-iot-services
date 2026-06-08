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
| 電圧測定回路 ch0 | 差動入力（AIN0-AIN1）、分圧回路 R_upper=680kΩ / R_lower=22kΩ、GAIN_EIGHT(±0.512V)、**メインバッテリー** |
| 電圧測定回路 ch1 | 差動入力（AIN2-AIN3）、分圧回路 R_upper=680kΩ / R_lower=22kΩ、GAIN_EIGHT(±0.512V)、**サブバッテリー** |
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

- `BLE Settings/Register` — BLE スキャンして SwitchBot デバイスを NVS 登録
- `BLE Settings/Remove` — 登録済みデバイスを NVS から削除
- `Battery/Ah Offset` — 積算電荷量のオフセット設定（BTN0 短押し +50Ah / 長押し -50Ah、0〜300Ah）
- `Battery/Ah Reset` — INA228 積算電荷量リセット（Yes/No 確認あり）
- `Battery/Chg Timeout` — 充電タイムアウト設定（10/20/30/60 分、BTN0 で循環、デフォルト 20 分）
- `Battery/Start Charge` — メインバッテリー充電開始（V_sub > V_main を確認。カウントダウン中は任意ボタンで停止）
- `Sensor View` — ADS1115/INA228 リアルタイム表示（50ms 更新）
- `System/Info` — FW バージョン・デバイス ID 表示
- `System/Device QR` — デバイス ID の QR コード表示
- `System/NVS Clear` — 全 NVS 消去して再起動（MQTT ホストは保持）
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
    │   ├── ble_peripheral.h/.cpp     BLE Peripheral（GATT 計測 Notify + 設定 R/W・Passkey ペアリング）
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
        ├── https.h/.cpp               HTTPS GET/PUT（AT+SH*）/ ファイルダウンロード（AT+HTTPTOFS）
        ├── ota.h/.cpp                 AWS IoT Jobs 確認・ファームウェア適用・ロールバック管理
        ├── jobs.h/.cpp                AWS IoT Jobs プロトコル層（subscribe / get-next / report）
        ├── command.h/.cpp             Jobs コマンドのディスパッチと実行（ah_reset, charge_main_batt）
        ├── shadow.h/.cpp              Shadow config publish・delta subscribe・設定変更の適用
        ├── monitor.h/.cpp             計測サイクル（measure() / publish()）・MeasureResult 定義
        ├── menu.h/.cpp                OLED + 2ボタン設定メニュー（enterMenuMode() → OperationMode）
        ├── menu_util.h/.cpp           メニュー用パスユーティリティ（pathPush / pathPop / pathTitle 等）
        ├── pubqueue.h/.cpp            オフラインバッファ（RTC メモリ + SPIFFS）・MQTT publish キュー管理
        ├── log_storage.h/.cpp         デバッグログ SPIFFS 保存（起動ごと 1 ファイル、最大 12 ファイル循環）
        └── logger.h/.cpp              シリアルデバッグ出力
└── lib/
    └── uzlib/                         gzip 圧縮・解凍ライブラリ（ESP32-targz 同梱版）
```

## データフロー

```text
ESP32-S3-MINI-1
  → MQTT over TLS（SIM7080G）
  → AWS IoT Core  topic①: sensors/{device_id}/data_bin  （バッテリーテレメトリ + BLE センサー、MessagePack）
                  topic②: $aws/things/{device_id}/shadow/update  （設定値 reported）
  → Topic Rule ingest_bin: SELECT encode(*,'base64') AS payload, topic(2) AS device_id FROM 'sensors/+/data_bin'
    → Lambda ingest（base64 decode → msgpack decode）→ S3: raw/year=YYYY/month=MM/day=DD/hour=HH/{device_id}-{uuid8}.json

クラウド → デバイス
  Shadow desired 更新 → $aws/things/{device_id}/shadow/update/delta → 設定値を NVS に適用
  IoT Jobs → ota.handleJob() / commandHandleJob()
```

## MQTT ペイロード形式

### バッテリーテレメトリ（sensors/{device_id}/data_bin）

**v1.15.0 以降は MessagePack 形式**でトピック `sensors/{device_id}/data_bin` に送信する。
通信経路上のフィールド名は短縮形を使用。`ingest` Lambda が base64 decode → MessagePack decode → キー展開して S3 に保存するため、Glue / Athena / Web 側は変更不要。

実測ペイロードサイズ: **59 bytes**（旧 JSON + フィールド名短縮 ~96 bytes → 約 38% 削減）

フィールド構成（MessagePack map。キーは下記 JSON 表記と同一）:

```json
{"t":"battery","m":12.34,"s":12.10,"i":5.2100,"p":62.500,"tp":28.5,"ah":200.001234,"ts":1746143400}
```

| 通信上のキー | S3 保存キー | 型 | 内容 |
| --- | --- | --- | --- |
| `t` | `type` | string | `"battery"` 固定 |
| `m` | `main` | float | メインバッテリー電圧（V）、ADS1115 ch0（AIN0-AIN1 差動） |
| `s` | `sub` | float | サブバッテリー電圧（V）、ADS1115 ch1（AIN2-AIN3 差動） |
| `i` | `current` | float | サブバッテリー電流（A）、INA228 |
| `p` | `power` | float | サブバッテリー電力（W）、INA228 |
| `tp` | `temp` | float | INA228 内蔵温度センサー（°C） |
| `ah` | `ah` | float | 積算電荷量（Ah）= INA228 積算値 + Ah オフセット（NVS） |
| `ts` | `ts` | int | UNIX タイムスタンプ（秒） |

### Shadow 設定値（reported / desired）

`$aws/things/{device_id}/shadow/update` に reported として publish する。

```json
{"state":{"reported":{"ah_offset":200,"chg_start_v":11.70,"chg_stop_v":12.50,"chg_min_diff_v":0.30,"charging":false,"override_next_mode":null,"fw_version":"1.17.0+xxxxxxxx"}}}
```

クラウドから desired を設定するとデバイスが次回起動時に delta を受け取り NVS に適用する。

```json
{"state":{"desired":{"ah_offset":200}}}
{"state":{"desired":{"chg_start_v":11.5,"chg_stop_v":12.8}}}
{"state":{"desired":{"chg_min_diff_v":0.5}}}
{"state":{"desired":{"charging":true}}}
{"state":{"desired":{"override_next_mode":"one_shot_continuous"}}}
```

`override_next_mode: "one_shot_continuous"` を設定すると、次回起動時に1サイクルだけ CONTINUOUS モードで動作（BLE アドバタイズ継続）し、自動で DEEP_SLEEP に戻る。デバイスが ACK として reported に `"one_shot_continuous"` を送信したタイミングで desired も自動クリアされる。

shadow publish はスリープ直前に1回だけ行う（起動時は行わない）。電源断で状態がズレた場合でも次サイクル（最大5分）で補正される。

### Jobs / OTA トピック（service/jobs が使用）

| トピック | 方向 | 用途 |
| --- | --- | --- |
| `$aws/things/{id}/jobs/$next/get` | publish | 次ジョブ取得リクエスト |
| `$aws/things/{id}/jobs/$next/get/accepted` | subscribe | ジョブ取得レスポンス |
| `$aws/things/{id}/jobs/{jobId}/update` | publish | ジョブ状態更新（IN_PROGRESS / SUCCEEDED / FAILED） |
| `$aws/things/{id}/shadow/update/delta` | subscribe | Shadow desired 変更の受信 |

## 主要クラスとインスタンス

| インスタンス | 型 | 役割 |
| --- | --- | --- |
| `lte` | `Lte` | SIM7080G ATコマンド制御・GPRS接続・証明書管理・ファイル読み取り・削除 |
| `mqtt` | `Mqtt` | MQTT publish / subscribe / pollMqtt |
| `https` | `Https` | AT+SH* 経由 HTTPS GET / ダウンロード |
| `ota` | `Ota` | AWS IoT Jobs 確認・FW 適用・ロールバック管理 |
| `bleScanner` | `BleScanner` | BLE スキャン・FreeRTOS キューへの書き込み |
| `blePeripheral` | `BlePeripheral` | BLE Peripheral（計測値 Notify・設定 R/W・Passkey ペアリング） |
| `bleTargets` | `BleTargets` | 監視対象 BLE アドレスの NVS 管理 |
| `button` | `Button` | BTN0/BTN1 デバウンス・長押し検出（`ButtonEvent`）・フィードバック音 |
| `logger` | `Logger` | Serial デバッグ出力（`printf`/`println`） |
| `ina228` | `Ina228` | INA228 I2Cドライバ（電流・電力・温度・積算電荷量の読み取り） |

ドメイン型:

| 型 | 役割 |
| --- | --- |
| `VoltageReading` | 電圧計測値（`float voltage`） |
| `PowerReading` | 電力計測値（`float current, power, temp, ah`） |
| `SensorReading` | アナログ計測値まとめ（`VoltageReading main, sub` + `PowerReading pwr` + `time_t ts`） |
| `MeasureResult` | 1サイクルの全計測結果（`SensorReading reading` + `SensorVariant ble[QUEUE_SIZE]` + `int bleCount`） |
| `ThermometerData` | SwitchBot 温湿度計データ（SensorBase + temp/humidity/battery） |
| `Co2MeterData` | SwitchBot CO2センサーデータ（ThermometerData + co2） |
| `SensorVariant` | `std::variant<ThermometerData, Co2MeterData>` |

## 重要な設計決定

### INA228 設計ノート

INA228 は VSSOP-10 パッケージ、I2C アドレス `0x40`（A0/A1=GND）。
ADS1115 と同じ I2C バス（SDA=GPIO17, SCL=GPIO18）に並列接続。

**起動時の初期化手順（毎回必須）：**

1. `CONFIG` レジスタを書き込む（ADCRANGE=1 + 変換時間 4120us + 平均化 1024 samples）
2. `SHUNT_CAL` レジスタを書き込む（VS リセットで消えるため DeepSleep 復帰後も必要）

**CONFIG レジスタ設定値：**

| フィールド | 設定値 | 内容 |
| --- | --- | --- |
| ADCRANGE (bit4) | 1 | ±40.96mV 入力レンジ |
| VBUSCT (bits[11:9]) | 0b111 | VBUS 変換時間 4120μs |
| VSHCT (bits[8:6]) | 0b111 | シャント電圧変換時間 4120μs |
| VTCT (bits[5:3]) | 0b111 | 温度変換時間 4120μs |
| AVG (bits[2:0]) | 0b111 | 平均化 1024 サンプル |

**SHUNT_CAL 計算（ADCRANGE=1）：**

```text
R_shunt = 75mV / 200A = 0.375mΩ
CURRENT_LSB = 208μA
SHUNT_CAL = 819.2×10^6 × CURRENT_LSB × R_shunt × 4（ADCRANGE=1 のため×4）
           = 819.2e6 × 208e-6 × 0.375e-3 × 4 ≒ 4096
ADCRANGE=1 → ±40.96mV、フルスケール電流 ±109A (I = V/R_shunt = 40.96mV / 0.375mΩ = 109A)
```

**注意事項：**

- A0/A1 はフローティング禁止（GND/SCL/SDA/VS のいずれかに接続）
- VS ピン直近に 100nF デカップリング必須
- VBUSはIN+と同ノードに接続（PCB上で直結）
- 起動時に CONFIG・SHUNT_CAL を毎回書き込む（電源 OFF でリセット）
- `readCurrent()` は符号反転済み（レジスタの増減方向が直感と逆のため）

詳細（部品比較・シャント設計）: `m5atom_power_adc/HARDWARE.md` の「サブバッテリー電流計測（INA228）」セクション参照。

### OLED ディスプレイ

`device/oled.h/.cpp` に SSD1306 ドライバを実装済み（I2C アドレス `0x3C`、SDA=GPIO17, SCL=GPIO18）。

| 関数 | 用途 |
| --- | --- |
| `oledInit()` | I2C 初期化・画面クリア |
| `oledPrint(text)` | 1行テキスト表示 |
| `oledShowSensorData(SensorReading&)` | 計測値全表示（main/sub/電流/電力/温度） |
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

| namespace | キー | 用途 |
| --- | --- | --- |
| `"device"` | `mqtt_host`, `debug_log` | MQTT ホスト、デバッグログ有効フラグ |
| `"lte"` | `cert_crc` | 証明書 CRC |
| `"ota"` | `job_id` | 保留中 OTA ジョブ ID |
| `"battery"` | `ah_offset`, `chg_timeout`, `chg_start_v`, `chg_stop_v`, `chg_min_diff_v` | Ah オフセット、充電タイムアウト（分）、充電開始電圧（V）、充電停止電圧（V）、最小 sub-main 電圧差（V） |
| `"switchbot"` | BLE アドレスキー | 監視対象 BLE デバイス一覧 |

### LTE / MQTT（SIM7080G + AWS IoT Core）

m5atom_iot_gateway と同一設計。以下の注意事項も継承:

- `modem.restart()` は使わず `modem.init()` を使う
- APN 設定は `CFUN=0 → CGDCONT → CFUN=1` の順（必須）
- DeepSleep 前に必ず `lte.disconnect()` → `lte.radioOff()` を呼ぶ

## config.h 定数一覧

| 定数 | 値 | 用途 |
| --- | --- | --- |
| `FIRMWARE_VERSION` | `"1.17.0+" GIT_HASH` | ファームウェアバージョン |
| `CHG_ON_PIN` | `21` | メインバッテリー充電制御ピン（HIGH=ON） |
| `GIT_HASH` | ビルド時注入（8文字 hex） | `extra_scripts.py` が `-DGIT_HASH` で定義 |
| `OperationMode` | enum class | `DEEP_SLEEP` / `CONTINUOUS` / `ONE_SHOT_CONTINUOUS`（動作モード） |
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
| 主要ライブラリ | TinyGSM, ArduinoJson, Adafruit SSD1306, Adafruit GFX, Adafruit ADS1X15, NimBLE-Arduino, QRCode |

## 作業中・引き継ぎ事項

### ~~TODO: Shadow データを S3/Athena に流す（時系列履歴の保存）~~ **設計変更により対応済み**

Shadow はテレメトリではなく設定値（ah_offset / fw_version）を管理するよう刷新。
バッテリーテレメトリは `sensors/{device_id}/data`（type=`"battery"`）として送信し既存の ingest パイプラインで S3 に蓄積。
Shadow の desired / delta による双方向リモート設定変更も実装済み。

### uzlib 圧縮・解凍 API（実機検証済み）

`lib/uzlib/` に圧縮・解凍両方の実装がある。

#### 解凍（gzip ファイルを展開）

```cpp
#include "../../lib/uzlib/uzlib.h"

static uint8_t dict[32768]; // 32KB スライディングウィンドウ（必須）
static uint8_t out[OUTPUT_SIZE];

TINF_DATA d = {};
d.source       = gzData;
d.source_limit = gzData + gzLen;
d.dest         = out;

uzlib_uncompress_init(&d, dict, sizeof(dict));
if (uzlib_gzip_parse_header(&d) != TINF_OK) { /* エラー */ }

int ret;
do { ret = uzlib_uncompress_chksum(&d); } while (ret == TINF_OK);
if (ret != TINF_DONE) { /* エラー */ }

size_t decompLen = d.dest - out;
```

#### 圧縮（gzip ファイルを生成）

`uzlib_compress` はデータのみ圧縮し、ブロックヘッダ・フッターは書かない。
**必ず `zlib_start_block` → `uzlib_compress` → `zlib_finish_block` の順で呼ぶ。**

```cpp
#include "../../lib/uzlib/uzlib.h"
#include "../../lib/uzlib/defl_static.h"

// 出力バイトを受け取るコールバック（writeDestByte が NULL だと出力が捨てられる）
static uint8_t s_deflateBuf[512];
static uint32_t s_deflateLen = 0;
static unsigned int deflateWriter(struct uzlib_comp *, unsigned char byte) {
    s_deflateBuf[s_deflateLen++] = byte;
    return 0;
}

// ハッシュテーブル（hash_bits=12 → 16KB）
static uzlib_hash_entry_t hashTable[1 << 12];
memset(hashTable, 0, sizeof(hashTable));
s_deflateLen = 0;

struct uzlib_comp comp = {};
comp.writeDestByte = deflateWriter; // 必須
comp.hash_table    = hashTable;
comp.hash_bits     = 12;

zlib_start_block(&comp);               // BFINAL=1, static Huffman ヘッダを書く
uzlib_compress(&comp, data, dataLen);  // LZ77+静的ハフマン圧縮
zlib_finish_block(&comp);             // end-of-block + バイト境界フラッシュ

// s_deflateBuf[0..s_deflateLen-1] に deflate ストリームが入っている
```

#### gzip ファイルの組み立て

```text
[10 bytes] gzip ヘッダ: 1F 8B 08 00 00 00 00 00 00 FF
[N bytes]  deflate ストリーム（上記の出力）
[4 bytes]  CRC32（LE）: uzlib_crc32(data, len, 0xffffffff) ^ 0xffffffff
[4 bytes]  元データサイズ（LE）: sizeof(data)
```

#### 注意事項

- `writeDestByte` が NULL だと出力が **完全に捨てられる**（outbuf に書かれない）
- `grow_buffer=1` にすると outbuf に書かれるが malloc/realloc が必要（組み込みには不向き）
- `zlib_start_block` を省くと BFINAL=0 の非終端ブロックになり、PC の解凍ツールが拒否する
- 0x00-0xFF の非圧縮データはほぼ圧縮できず deflate 出力が入力より大きくなる（正常動作）

---

### ~~OTA ファームウェアの gzip 圧縮~~ **v1.13.0 で実装済み**

`deploy_ota.ps1 -Compress` で `firmware.bin.gz` を S3 にアップロード。
`ota.apply()` が URL 末尾 `.gz` を検出し、uzlib ストリーミング解凍しながら書き込む。
`ota.handleJob()` に `force=true` フラグを追加（バージョン一致でも強制更新）。

**実装ポイント（uzlib ストリーミング）**: `source = NULL` にして `readSourceByte` コールバックを使う。
コールバック内で `lte.fileReadChunk()` を 4096 バイト単位で同期取得してバッファを補充する。
`destSize` に出力チャンクサイズを設定し `uzlib_uncompress_chksum()` を繰り返す。

**事前検証テスト（再作成手順）**:
0x00-0xFF の 256 バイトパターンを gzip 圧縮・S3 PUT・GET・解凍・検証するテスト。

1. 一時 S3 バケットを作成（公開 PUT/GET ポリシー）し `test_data.gz` をアップロード
2. 以下の Python で gzip ファイルを生成: `data = bytes(range(256)); gzip.open(out,'wb').write(data)`
3. デバイス側: `TINF_DATA` に `source`/`dest`/`dict` を設定してヘッダパース → 解凍ループ → 検証
4. gzip ヘッダ: `1F 8B 08 00 00 00 00 00 00 FF` + deflate + CRC32(LE) + size(LE)
5. 注意: 0x00-0xFF は非圧縮に近く deflate 出力が入力より大きくなる（正常）

### ~~フェーズ 2: AWS IoT からのコマンド受信~~ **AWS IoT Jobs で実装済み**

- `service/jobs.h/.cpp`: Jobs プロトコル層（subscribe / get-next / report）
- `service/command.h/.cpp`: コマンドディスパッチ（`operation` フィールドで振り分け）
- 実装済みコマンド: `ah_reset`、`charge_main_batt`（`timeout_sec` 指定、省略時 1200 秒）
- 未実装コマンド: `relay_on` / `relay_off`（リレーピン定数の整理が必要）
- 設定変更は Jobs ではなく Shadow desired/delta で行う（`ah_offset`、`chg_start_v`、`chg_stop_v` 等）

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
- 積算電荷量のリセット（メニューの `Battery/Ah Reset` から手動実行可能）

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

### TODO: BLE ダッシュボード表示器（未着手）

ESP32-S3 を BLE Central として動かし、運転席から視認できる外付けディスプレイに計測値をリアルタイム表示する。

**ハードウェア候補**:

- **CYD（ESP32-2432S028）**: 2.8インチTFT、ESP32内蔵、~$15、PlatformIO 対応済み。視認性・価格のバランスが良くファーストチョイス
- LilyGO T-Display-S3 AMOLED（2.41インチ）: 輝度・発色が高く直射日光に強い

**表示項目（案）**:

- バッテリー電圧 / 電流 / 電力（INA228 計測値）
- 接続状態（BLE / LTE）

**実装方針**:

- ESP32-S3 側: NimBLE で BLE Peripheral（GATT Server）を追加。計測値を Notify で送信
- CYD 側: NimBLE で BLE Central（GATT Client）を実装。受信データを TFT に描画
- 既存の LTE / MQTT 処理とは非同期で動作させる（`millis()` ベースで一定周期送信）

### ~~TODO: スマホ BLE 連携~~ **本番実装完了**

ESP32-S3 を BLE Peripheral として動かし、スマホから設定・監視を行う。

**方針**: Flutter アプリ（iOS / Android 両対応）。`mobile/` ディレクトリに実装済み。

**実装順序**:

1. ~~**接続検証**~~（完了）
2. ~~**計測値ダッシュボード**~~（完了）
3. ~~**設定 Read/Write**~~（完了）
4. ~~**本番実装**~~（完了）: `device/ble_peripheral.h/.cpp` として本体に組み込み
5. ~~**DeepSleep + GPIO0 EXT0 wakeup 登録**~~（完了）

#### 本番 GATT 構成

デバイス名: `car-iot-ble`、スキャンフィルタ: 計測サービス UUID

**計測サービス** — Notify のみ、全値 float32 (little-endian)、認証不要

| Characteristic | UUID | 型 | 内容 |
| -------------- | ---- | -- | ---- |
| 計測サービス | `f3a8b2c1-d4e5-4f6a-7b8c-9d0e1f2a3b4c` | — | Service |
| メイン電圧 | `f3a8b2c2-...` | float32 | V（adsReadDiffMain） |
| 電流 | `f3a8b2c3-...` | float32 | A（ina228.readCurrent） |
| 電力 | `f3a8b2c4-...` | float32 | W（ina228.readPower） |
| サブ電圧 | `f3a8b2c5-...` | float32 | V（adsReadDiffSub） |

**設定サービス** — READ_AUTHEN / WRITE_AUTHEN（MITM ペアリング後のみ R/W 可）

| Characteristic | UUID | 型 | 内容 |
| -------------- | ---- | -- | ---- |
| 設定サービス | `f3a8b2d1-d4e5-4f6a-7b8c-9d0e1f2a3b4c` | — | Service |
| Ah オフセット | `f3a8b2d2-...` | int32 | Ah（`getAhOffset` / `setAhOffset`） |
| 充電タイムアウト | `f3a8b2d3-...` | uint32 | 分（`getChgTimeoutMin` / `setChgTimeoutMin`） |
| 充電開始電圧 | `f3a8b2d4-...` | float32 | V（`getChgStartV` / `setChgStartV`） |
| 充電停止電圧 | `f3a8b2d5-...` | float32 | V（`getChgStopV` / `setChgStopV`） |

#### 動作フロー

- 通常起動: setup() で BLE アドバタイズ開始（認証なし接続 → 計測 Notify のみ）
- スマホ接続 → CONTINUOUS モードに昇格（5分おきに measure + publish + notify）
- スマホ切断 → DEEP_SLEEP に戻る（手動 CONTINUOUS の場合は維持）
- DeepSleep 前に `rtc_gpio_pullup_en(GPIO_NUM_0)` + `esp_sleep_enable_ext0_wakeup(GPIO_NUM_0, 0)` を登録済み。BOOT ボタン押下で DeepSleep から即時復帰できる

#### Phone メニュー（ペアリング）

- OLED メニュー「Phone」を選択 → 6桁の Passkey を OLED に表示
- スマホ側でコードを入力してペアリング → CONTINUOUS モードに移行
- ボンディング情報は NVS に保存（`MAX_BONDS=1`：再ペアリングで上書き）
- ペアリング後は次回から自動接続 + 設定 R/W 可能

### TODO: ULP による低電圧アラート起動（未着手）

ULP RISC-V コプロセッサで DeepSleep 中もバッテリー電圧を監視し、閾値以下になったらメインCPU を起こして LTE でアラートを送信する。

**ハードウェア**:

- Grove コネクタ（GPIO7/GPIO8）に抵抗分圧回路を接続（100k + 27k、12V → 2.55V）
- GPIO7 = ADC1 ch6 → ULP RISC-V から直接読める

**実装方針**:

- `ulp/main.c` に ULP RISC-V コードを追加（C で記述）
- 30秒ごとに ADC1 ch6 を読み取り、閾値以下なら `ulp_riscv_wakeup_main_processor()`
- `platformio.ini` に `board_build.ulp_type = ulp_riscv` を追加
- DeepSleep 移行時に ULP を起動し `esp_sleep_enable_ulp_wakeup()` を設定
- `setup()` の wakeup 判定に `ESP_SLEEP_WAKEUP_ULP` を追加 → 低電圧アラート送信

**閾値（案）**: ADC raw 値 1800 ≈ 11V（要キャリブレーション）

### ~~TODO: 通信量削減 Phase 1（フィールド名短縮）~~ **実装済み**

MQTT 通信経路上のフィールド名を短縮し、送信バイト数を削減した（MQTT フレーミング込み ~156 bytes → ~132 bytes）。

**設計方針**: 短縮名は通信ドライバ層（`domain/telemetry.cpp`）のみに閉じ込める。`ingest` Lambda で受信時にフルネームへ展開して S3 に保存するため、Glue / Athena / Web は変更不要。

**フィールド名マッピング**（通信上の短縮名 → S3 保存名）:

| 通信上 | S3/内部 | 通信上 | S3/内部 |
| --- | --- | --- | --- |
| `t` | `type` | `a` | `addr` |
| `m` | `main` | `h` | `humidity` |
| `s` | `sub` | `bt` | `battery` |
| `i` | `current` | `rs` | `rssi` |
| `p` | `power` | `tp` | `temp` |

`ah` / `ts` / `co2` / `mf` / `fw` は変更なし。

### ~~TODO: 通信量削減 Phase 2（MessagePack 化）~~ **v1.15.0 で実装完了**

**実装内容**:

- `domain/telemetry.h/.cpp`: `ITelemetryEncoder` 基底クラス（Template Method）+ `JsonTelemetryEncoder` / `MsgPackTelemetryEncoder`
- `service/mqtt.h/.cpp`: `publish(topic, uint8_t*, size_t)` でバイナリ送信（`SerialAT.write()` 使用）
- `service/pubqueue`: エンコーダを DI で切り替え。MsgPack 時はトピック `sensors/{id}/data_bin` を使用
- `platformio.ini`: `release` / `develop` env ともに `-D USE_MSGPACK` 有効（デフォルト）
- `infra/iot.tf`: `sensors/+/data_bin` 用 Topic Rule `ingest_bin`（`encode(*,'base64')` 経由）+ Lambda permission 追加
- `infra/lambda_src/ingest/index.py`: インライン msgpack デコーダ（stdlib のみ）+ `payload` キー分岐

**効果（実測）**（バッテリーテレメトリ 1 件）:

| 形式 | ペイロード |
| --- | --- |
| JSON + フィールド名短縮（旧） | ~96 bytes |
| MessagePack + フィールド名短縮（現行） | **59 bytes**（約 38% 削減） |

**確認済み**（2026-05-26）: `AT+SMPUB` バイナリ送信、Lambda でのデコード、管理画面でのデータ表示すべて正常動作。

### TODO: ストリーミング OTA（塩漬け）

現在の OTA は「SIM7080G FS にフル DL してから読み返す」2フェーズ構成。`AT+HTTPREAD` を使ってチャンクを HTTP レスポンスから直接読み出し、解凍→フラッシュ書き込みを同時進行させることで Phase2 の往復を丸ごと削減できる。

**現行 2フェーズの利点（塩漬けの理由）**: DL 完了後に書き込みを開始するため、通信断が起きてもフラッシュは一切汚れない。ストリーミング化すると DL 中断時に `esp_ota_abort()` 処理が必要になりエラーパスが増える。

**実装方針**:

- `service/https.cpp` の download API を廃止し、ストリーミングコールバック API を追加
- `AT+HTTPTOFS` → `AT+HTTPACTION` + `AT+HTTPREAD` に変更し、受信チャンクをそのまま uzlib へ渡す
- SIM7080G の `AT+HTTPREAD` の最大チャンクサイズ（1460 bytes）に合わせてバッファを調整

### ~~TODO: NimBLE ペリフェラル・ブロードキャスター無効化~~ **実装済み**

`platformio.ini` の release env に `CONFIG_BT_NIMBLE_ROLE_PERIPHERAL_DISABLED=1` / `CONFIG_BT_NIMBLE_ROLE_BROADCASTER_DISABLED=1` を追加。Flash **約16KB 削減**（696,693 → 680,165 bytes）。

「BLE ダッシュボード表示器」「スマホ BLE 連携」TODO を実装する際は Peripheral が必要になるため、その時点で削除する。

### TODO: バッテリー上がりアラート（未着手）

**実装方針**: ingest Lambda 内で条件評価 → SNS 通知

MsgPack decode が既に済んだ状態で全フィールドが展開されているため、条件判定を数行追加するだけで実装できる。SNS トピックを追加して通知先（メール等）を繋ぐ。

アラート状態（OK / WARNING / CRITICAL）は **SSM パラメータストア**に保存し、状態変化（OK→ALARM）のときのみ SNS を発火する。条件解消時は回復通知も送る。5分ごとの read/write は月 720 回程度でスタンダードパラメータの無料枠（10,000 回/月）内に収まる（DynamoDB はコストが見合わないため不採用）。

閾値も SSM パラメータストアで管理し、Lambda 環境変数 `ALERT_PROFILE`（`prod` / `test`）でプロファイルを切り替える。

| パラメータ | prod | test（現在の実測値が発火するよう設定） |
| --- | --- | --- |
| `ah_low` | 20 | 195 |
| `ah_high` | 40 | 200 |
| `m_high` | 12.2 | 13.5 |
| `m_low` | 12.0 | 13.0 |

SSM パスの例: `/car-iot/alert/{profile}/ah_low`

**発火条件**:

| 条件 | レベル |
| --- | --- |
| `ah < 20` | 緊急 |
| `ah < 40 AND m < 12.2 AND (s - m) >= 0` | 警告 |
| `m < 12.0 AND (s - m) >= 0` | 緊急 |

- `s - m >= 0`（= B V >= 0）はエンジンOFF状態の判定。エンジンON時はオルタネーター充電でmain電圧が14V台になるため誤検知を防ぐ
- 警告条件（`ah < 40 AND m < 12.2`）は sub・main ともに低下中で緊急充電回路が diff 不足により機能していない可能性がある
- 緊急条件（`m < 12.0`）は充電回路が機能しないまま main が警戒ライン（エンジン始動リスク）に達した状態
- フィールド名は MsgPack キー（`m`=main電圧, `s`=sub電圧, `ah`=サブAh）

**不採用案**:

- Grafana Alerting：アラートチェックのたびに Athena クエリが発行されコストがかかる。Grafana の可用性依存も懸念
- IoT Core ルールエンジン直接評価：MsgPack（バイナリ）を直接パースできないため不可
- EventBridge Scheduler + 専用 Lambda + SNS：柔軟で既存コードと分離できるが、別 Lambda の実装コストがかかる

**背景**: sub（LiFePO4）が深放電に至ると、v1.1.0基板のMOSFETボディダイオード経由でmain（鉛バッテリー）が12Vバスの負荷を供給し続け、mainが上がるリスクがある。梅雨期間の長期曇天でソーラー発電が途絶えた場合に現実的なリスクとなる。

### TODO: v2.0.0 基板 — 電源ボタン＋自己保持回路（未着手）

緊急時（main 電圧が危機的水準に達したとき）に ESP32 から回路全体の電源を完全に断てるよう、次世代基板（v2.0.0）に自己保持回路を追加する。

**回路構成（案）**:

```text
Sub(+) 12-13V
  │
  ├── R1(100k) ──── P-ch FET [Gate] ──┬── [電源SW モーメンタリ] ── GND
  │                                    └── [NPN C]
  │                                        [NPN B] ── R2(10k) ── ESP32 GPIO(PWR_HOLD)
  │                                        [NPN E] ── GND
  │
  └────────────── P-ch FET [Source]
                          │
                       [Drain] ── VIN ── [LM2596S] ── 5V ── [ESP32]
GND
```

**動作シーケンス**:

```text
【起動】電源SW押下 → FET Gate が GND へ → Vgs=-12V → FET ON
        → VIN供給 → LM2596S → 5V → ESP32起動
        → ESP32 PWR_HOLD = LOW → NPN ON → Gate を GND に保持（自己保持）
        → SW離しても FET ON 維持

【停止】ESP32 PWR_HOLD = HIGH → NPN OFF
        → R1 が Gate を Sub(+) へ引き上げ → Vgs=0 → FET OFF
        → VIN断 → LM2596S停止 → ESP32停止
        → NPN も OFF → 状態維持（発振しない）

【復帰】電源SW押下のみ（手動）
```

> LM2596S の ON/OFF ピン制御は「ESP32が落ちると R101 でON/OFFピンが GND に戻り再起動する」発振問題があるため不採用。VIN 自体を FET で遮断するこの構成が正しい。

**目的**: DeepSleep では ESP32 の消費はほぼゼロになるが LM2596S は動き続けるため、ボディダイオード経由の電流パスが残る。完全電源断によって 12V バスへの消費を完全に止め、main バッテリーへの影響をゼロにする。

**前提**: アラートが機能すれば main 12V 以下になる前に人が対処できる。この回路はアラートが届かなかった最悪ケースへの安全ネット。製造コスト上の制約から、BJT 化（ボディダイオード解消）と同一ロットで発注する。
