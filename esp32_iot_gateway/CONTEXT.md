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
| LTE ピン | RX=GPIO7 ← U128 TXD、TX=GPIO8 → U128 RXD（筐体都合によりGrove Unit 1経由。基板シルク上のLTE_RX/TX/ENラベルとは不一致） |
| LTE 電源 | GPIO9（LTE_EN: HIGH=ON、AO3401A パワースイッチ経由） |
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
    │   ├── telemetry.h/.cpp           Shadow設定ペイロード組み立て + ITelemetryEncoder（Json/MsgPack、MsgPackはmagic+CRC32付与）
    │   ├── sensor.h                   BLE センサー共通構造体（SensorBase）
    │   ├── thermometer.h/.cpp         SwitchBot 温湿度計パーサー
    │   ├── co2meter.h/.cpp            SwitchBot CO2センサーパーサー
    │   ├── sensor_factory.h/.cpp      センサー種別振り分け（SensorVariant）
    │   ├── sensor_filter.h/.cpp       BLE センサー値のメディアンフィルタ（BLE_MEDIAN_FILTER時のみ）
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

#### バイナリフレーミング（v1.18.0以降・パケット破損対策）

`sensors/{device_id}/data_bin` の実際の送信バイト列は、上記 MessagePack map をさらに以下でラップする:

```text
[magic 1B: 0xC1][version 1B][MessagePack本体（上記map）][CRC32 4B（little-endian、magic~本体まで）]
```

- **背景**: ESP32↔SIM7080G間のUARTにパリティ・CRCが無く、稀に1ビット化けがそのままクラウドまで届く問題があった。Shadow reportedのキー名が化けて大量蓄積する形で発覚（Shadow設定値は本節で後述の通りJSONテキストのため同種の対策は未実装。破損が疑われる場合はDevice Shadowを一度`delete-thing-shadow`すれば次サイクルで自動再構築される）
- **magic 0xC1**: MessagePack仕様で「未使用」と規定されたバイト値。本体（常にfixmap: `0x80`-`0x8F`始まり）と衝突しないため、`ingest` Lambda はこのバイトの有無だけで新旧フォーマットを自動判別できる（**ファーム/Lambdaのデプロイ順序に依存しない**）
- **旧フォーマット（v1.17.0以前）**: ヘッダ無し、MessagePack本体のみ。`ingest` Lambda は今後も後方互換としてサポートし続ける
- **CRC不一致時**: `raw/`（Athenaスキーマ）には保存せず、生バイナリのまま専用バケット（`corrupted`、30日で自動削除）に退避する。`ingest` Lambda のログに `[CORRUPT]` として理由を出力
- S3保存JSONには検出したフォーマットバージョンを `"ver"`（0=旧形式、1=新形式）として追加する

### Shadow 設定値（reported / desired）

`$aws/things/{device_id}/shadow/update` に reported として publish する。

```json
{"state":{"reported":{"ah_offset":200,"chg_start_v":11.70,"chg_stop_v":12.50,"chg_min_diff_v":0.30,"charging":false,"override_next_mode":null,"continuous_until_time":null,"fw_version":"1.23.0+xxxxxxxx"}}}
```

クラウドから desired を設定するとデバイスが次回起動時に delta を受け取り NVS に適用する。

```json
{"state":{"desired":{"ah_offset":200}}}
{"state":{"desired":{"chg_start_v":11.5,"chg_stop_v":12.8}}}
{"state":{"desired":{"chg_min_diff_v":0.5}}}
{"state":{"desired":{"charging":true}}}
{"state":{"desired":{"override_next_mode":"timed_continuous","continuous_until_time":1746143700}}}
```

`override_next_mode: "timed_continuous"` を `continuous_until_time`（継続期限、絶対UNIXタイムスタンプ）と同時に設定すると、次回起動時からその時刻に達するまで CONTINUOUS サイクルを繰り返し、期限到達後に自動で DEEP_SLEEP に戻る（BTN1 長押しでも即座に DEEP_SLEEP へ切り替え可能）。`continuous_until_time` 未指定時はデフォルト30分後。上限は現在時刻から1440分（24時間）後にクランプされる（無期限化を防ぐ安全策）。管理画面では「今から何分後」を入力し、送信時にJS側で絶対時刻へ変換してPUTする。

`continuous_until_time` は `ah_offset` 等と同様に**TIMED_CONTINUOUS中は継続してreportedに反映され続ける**（`shadowPublishConfig()`が呼ばれるたびに現在の期限を送る）ため、管理画面を開き直しても「今設定されている期限」を確認できる。DEEP_SLEEP等TIMED_CONTINUOUS以外のモードになった時点で自動的に`null`が送られる（期限到達・BTN1長押し・BLE切断のいずれの経路でDEEP_SLEEPに戻っても、次にshadow publishされたタイミングでnullになる）。

デバイスが ACK として reported に `"timed_continuous"` を送信したタイミングで desired も自動クリアされる。`override_next_mode` は `setup()` 時にしか反映されない（稼働中の即時切り替えは非対応）。CONTINUOUS/TIMED_CONTINUOUS中も5分サイクルごとにOTA/コマンドJobsを確認するため（`checkAndHandleJob()`、[main.cpp](esp32_iot_gateway/src/main.cpp)参照）、長時間の継続中でもOTAは通常通り届く。

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
| `ITelemetryEncoder` | テレメトリエンコーダ基底クラス（`encodeBattery`/`encodeThermometer`/`encodeCo2`を実装、`serialize`は派生クラスに委譲） |
| `JsonTelemetryEncoder` | JSON形式エンコーダ（トピック`data`） |
| `MsgPackTelemetryEncoder` | MessagePack形式エンコーダ（トピック`data_bin`）。`[magic 0xC1][version][本体][CRC32 4B]`でラップ |

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
| `"device"` | `mqtt_host`, `board_version`, `debug_log` | MQTT ホスト、基板バージョン（provisioning時に書き込み、`getBoardVersion()`。未設定時は1）、デバッグログ有効フラグ |
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
| `FIRMWARE_VERSION` | `FIRMWARE_VERSION_BASE "+" GIT_HASH` | ファームウェアバージョン。`FIRMWARE_VERSION_BASE`はBOARD_VERSIONで基板シリーズ別に分岐（1=v1基板、2=v2基板）し、ソースが正。gitタグはCI発火・GitHub Release表示用のみ。詳細は`RELEASE.md`参照 |
| `GIT_HASH` | ビルド時注入（8文字 hex） | `extra_scripts.py` が `-DGIT_HASH` で定義 |
| `OperationMode` | enum class | `DEEP_SLEEP` / `CONTINUOUS` / `TIMED_CONTINUOUS`（動作モード） |
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

device/lte.h の定数（ピン番号は `board_pins.h` 経由、下記参照）:

| 定数 | 値 | 用途 |
| --- | --- | --- |
| `APN` | `"soracom.io"` | SORACOM APN |
| `APN_USER` | `"sora"` | SORACOM APN ユーザー |
| `APN_PASS` | `"sora"` | SORACOM APN パスワード |
| `SEND_INTERVAL_SEC` | `60` | デバッグモード送信間隔（秒） |

## board_pins.h / 基板バージョン切り替え

ピン番号は `src/board_pins.h` の `BoardPins` 構造体に集約し、`boardPins()` で取得する（`config.h` と同格で全層から参照可）。
`m5atom_power_adc` の基板バージョン（v1/v2）ごとに `board_pins_v1.h` / `board_pins_v2.h` が実値を持ち、
`board_pins.h` が `BOARD_VERSION`（`platformio.ini` の `build_flags` で指定、未指定時は `1`）に応じて該当ヘッダを `#include` する（選ばれなかった方はコンパイラに一切渡らない）。

| 基板 | gu0x系（未使用） | gu1x系（LTE接続） | Relay0/1/2 | PWR_HOLD | GP2/GP3・GP11/GP12 |
| --- | --- | --- | --- | --- | --- |
| v1 | GPIO4/5/6 | GPIO7/8/9 | GPIO11/13/15 | 非搭載（`PIN_UNUSED`） | 非搭載 |
| v2 | GPIO4/5/6 | GPIO7/8/9 | GPIO13/14/15 | GPIO10 | GPIO2/3・GPIO11/12 |

LTEモジュールは筐体都合で実際には gu1x 系ピン（GPIO7/8/9）に接続されており、回路図上の `LTE_RX`/`LTE_TX`/`LTE_EN` ラベル（IO4-6）とは一致しない。

## ビルド環境

| 項目 | 内容 |
| ---- | ---- |
| IDE | PlatformIO |
| プラットフォーム | espressif32 |
| ボード | esp32-s3-devkitc-1（ESP32-S3-MINI-1 互換） |
| C++ 標準 | C++17（`-std=gnu++17`） |
| env | `esp32-s3-devkitc-1-v1-develop` / `-v2-develop` / `-v1-release` / `-v2-release`（デフォルトは `v2-develop`。release envはOTA配信もv1/v2で別パイプライン、`RELEASE.md`参照） |
| ビルドフック | `extra_scripts.py`（`pre:`）— git hash を `GIT_HASH` マクロとして注入 |
| 主要ライブラリ | TinyGSM, ArduinoJson, Adafruit SSD1306, Adafruit GFX, Adafruit ADS1X15, NimBLE-Arduino, QRCode |

## 作業中・引き継ぎ事項

> 実装済み/対応済みになったTODOは `CONTEXT_ARCHIVE.md` に移動している。

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

### TODO: corrupted バケットの破損データをCRC32ブルートフォースで訂正（未着手）

これまで観測した破損は一貫して1パケットにつき1ビットだけ反転するパターン（Shadow reportedで化けたキー名も元のキーと1〜2ビットしか違わない）。この特性を利用し、**ファーム変更・ペイロード増量なしで既存のCRC32だけを使った訂正**が可能。

**方式**: `corrupted` バケットに退避された生バイナリ（`raw`）に対し、`raw[:-4]`（magic+version+本体）の全ビット位置を1つずつ反転させて再計算し、末尾4バイトのCRC32と一致する候補を探す。ペイロードは高々500〜700ビット程度なので探索コストは無視できるレベル。候補が一意に一致すれば、CRC32が偶然一致する確率（候補数 × 2^-32 ≈ 10^-7）はほぼゼロなのでその訂正はほぼ確実に正しい。2ビット以上の破損だった場合は一致候補が見つからず、現状通り「訂正不可・`corrupted`に残る」に自然にフォールバックする（誤訂正のリスクなし）。

**実装方針（案）**:

- `ingest` Lambda の `_save_corrupted` 呼び出し箇所（またはオフライン解析スクリプト）に訂正トライを追加
- 既に `corrupted` バケットに溜まっている過去データにも遡って適用可能
- Hamming/Reed-Solomon等の本格的なECCをファームに実装する案もあったが、テレメトリは5分おき送信で1件欠損の実害が小さいこと、実装コスト・新規依存追加のリスクを考えると見合わないため不採用

### TODO: compaction後データの行単位削除対応（未着手、[S3 raw データの定期 Compaction](CONTEXT_ARCHIVE.md#todo-s3-raw-データの定期-compaction-実装済み)に従属）

[S3 raw データの定期 Compaction](CONTEXT_ARCHIVE.md#todo-s3-raw-データの定期-compaction-実装済み)を実装すると、`DELETE /data` の行単位削除（`infra/lambda_src/delete/index.py` の `_delete_by_keys`）がマージ済みファイルに対しては機能しなくなる（ファイル全体＝該当時間帯の全行が削除対象になってしまう）。

**実装方針（案）**: `_delete_by_keys` で対象keyがマージ済みNDJSONファイルかどうかを判定し、マージ済みなら `GetObject` → 削除対象行を除いて残りを `PutObject` で上書きする分岐を追加する。ファイルサイズは compaction 後も小さいままの想定なので、読み直しコストは軽微。判定方法（ファイル名規則 or 内部メタデータ）は compaction 実装時に合わせて設計する。

### TODO: ストリーミング OTA（塩漬け）

現在の OTA は「SIM7080G FS にフル DL してから読み返す」2フェーズ構成。`AT+HTTPREAD` を使ってチャンクを HTTP レスポンスから直接読み出し、解凍→フラッシュ書き込みを同時進行させることで Phase2 の往復を丸ごと削減できる。

**現行 2フェーズの利点（塩漬けの理由）**: DL 完了後に書き込みを開始するため、通信断が起きてもフラッシュは一切汚れない。ストリーミング化すると DL 中断時に `esp_ota_abort()` 処理が必要になりエラーパスが増える。

**実装方針**:

- `service/https.cpp` の download API を廃止し、ストリーミングコールバック API を追加
- `AT+HTTPTOFS` → `AT+HTTPACTION` + `AT+HTTPREAD` に変更し、受信チャンクをそのまま uzlib へ渡す
- SIM7080G の `AT+HTTPREAD` の最大チャンクサイズ（1460 bytes）に合わせてバッファを調整

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

### TODO: OTA中のBLE無効化（IPCタスクスタックオーバーフロー対策・未着手）

現状、OTA（`ota.handleJob()` → `apply()`）中も BLE（`blePeripheral` / `bleScanner`）は動いたまま。`esp_ota_write` によるフラッシュ書き込みとBLEスタックが同時に動くと、ESP32/ESP32-S3で知られる "IPC task has overflowed its stack" の要因になり得る。

[CONTINUOUSモード中はOTAジョブを再チェックしない問題](CONTEXT_ARCHIVE.md#todo-continuousモード中はotaジョブを再チェックしない問題-対応済み)が対応済みになり、`CONTINUOUS`/`TIMED_CONTINUOUS`中も5分サイクルごとにOTAを検知するようになったため、このBLE無効化未対応リスクが顕在化する頻度は以前より上がっている。

**実装方針**:

- `blePeripheral.stop()`（`NimBLEDevice::stopAdvertising()` のみ、`device/ble_peripheral.cpp`）と
  `bleScanner.deinit()`（Peripheral と共存するため no-op、`device/ble_scan.cpp`）は、
  どちらも NimBLE ホストタスク・コントローラ自体は停止しないため、この対策には**使えない**（レビュー指摘済み）
- 実際に BLE スタックを完全停止するには `NimBLEDevice::deinit(true)`（引数 true でコントローラも解放）を呼ぶ必要がある。
  `service/ota.cpp` の `handleJob()` 冒頭（`jobsReport(job.id, "IN_PROGRESS")` 直後あたり）で呼ぶ想定
- `NimBLEDevice::deinit(true)` の後に BLE を再開するには `BleScanner::setup()` 相当
  （`NimBLEDevice::init()` からのアドバタイズコールバック再設定を含む）をやり直す必要がある。
  現状の `setup()` は起動時 1 回のみの呼び出しを想定した作りのため、再入可能にする見直しが必要
- OTA成功時は `esp_restart()` するため再開処理は不要。失敗時に呼び出し元へ処理が戻るケースでBLEを再開すべきか、
  その場合の再初期化方法が検討課題として残る

### v2.0.0 基板 — 電源ボタン＋自己保持回路（ファームウェア側は実装済み、電源断ロジックは未着手）

緊急時（main 電圧が危機的水準に達したとき）に ESP32 から回路全体の電源を完全に断てるよう、次世代基板（v2.0.0）に自己保持回路を追加する。
PWR_HOLD は `board_pins.h`（`BoardPins::pwrHoldPin`、v2 = GPIO10）で管理し、`main.cpp` の `setup()` 冒頭での HIGH アサート・`enterDeepSleepMode()` での `gpio_hold_en()` は実装済み（`BOARD_VERSION == 2` でのみ有効）。
main 電圧低下時に PWR_HOLD を LOW にして能動的に電源を落とす判定ロジックは未実装。

**回路構成（案）**:

```text
Sub(+) 12-13V
  │
  ├────────────────────── FET [S]
  │                            ↓ (P-ch: G が S より低いと ON)
  R1(100k)                FET [D] ── VIN ── [LM2596S] ── 5V ── [ESP32]
  │                                                                  │
  ●─┬─ FET [G]                                         GPIO(PWR_HOLD)
    ├─ NPN [C]                                                       │
    │  NPN [B] ──── R2(10k) ─────────────────────────────────────── ┘
    │  NPN [E] ── GND
    │
    ├─ [電源SW] ── GND
    │
    └─ R3(100Ω) ── C1(47μF) ── GND
```

> ●（ゲートノード）に R1・FET[G]・NPN[C]・電源 SW・R3+C1 が合流する。
> C1 は ● に常時接続することで SW 解放後もゲート上昇を遅延させ（τ = R1×C1 ≈ 5秒）、ESP32 が自己保持を確立するまでの時間を稼ぐ。R3 は SW 押下時に C1 が SW 接点経由で急放電するのを防ぐ電流制限（ピーク 12V/100Ω = 120mA）。
> R1 は通電中ずっと Sub(+)→GND に電流を流す（0.12mA、1.4mW）。完全電源断後はこの電流もゼロになる。

**動作シーケンス**:

```text
【起動】電源SW押下 → ●(Gate) が GND へ → FET ON → VIN供給 → LM2596S → 5V → ESP32起動
        → setup() 冒頭で PWR_HOLD = HIGH → NPN 導通 → Gate を GND に保持（自己保持）
        → SW 離してもゲートは NPN で GND に固定 → FET ON 維持

【停止】PWR_HOLD = LOW → NPN オフ
        → R1 が Gate を Sub(+) へ引き上げ → FET OFF → VIN断 → ESP32停止
        → NPN も自然オフ → 状態維持（発振しない）

【復帰】電源SW押下のみ（手動）
```

> LM2596S の ON/OFF ピン制御は「ESP32が落ちると ON/OFF ピンが GND に戻り再起動する」発振問題があるため不採用。VIN 自体を FET で遮断するこの構成が正しい。

**FET 選定条件**:

| パラメータ | 要件 | 根拠 |
| --- | --- | --- |
| Vgs(max) | **±20V 以上**（または Zener クランプ） | LiFePO4 満充電時 Sub(+) ≈ 14.6V → Vgs = -14.6V。AO3401A の ±12V では破損する |
| Id(cont.) | 1A 以上 | ボード全体のピーク消費：ESP32+SIM7080G TX+リレー ≈ 500mA |
| Rds(on) | 問題にならない | 500mA × 0.1Ω = 50mW 程度。SOT-23 で十分放熱できる |
| パッケージ | SOT-23 可 | 500mA 以下であれば SOT-23 の熱抵抗（500℃/W）でも問題ない |

**候補 A（FET 変更）**: Vgs(max) = ±20V の P-ch FET（例: DMG3415U など）に変える。

**候補 B（AO3401A 流用 + Zener クランプ）**: Gate-Source 間に 12V Zener（BZX84C12 等、SOT-23）を追加して Vgs を -12V に制限する。R1 = 100kΩ のためツェナー電流 ≈ (14.6-12)/100k = 26μA と微小で発熱なし。

**実装上の注意**（`main.cpp` に実装済み）:

- PWR_HOLD は RTC 対応ピン（ESP32-S3: GPIO 0〜21、v2 = GPIO10）を使う。deep sleep 前に `gpio_hold_en()` で HIGH を維持し、起動直後に `gpio_hold_dis()` で解除する（CHG_ON_PIN と同じパターン）
- `digitalWrite(PWR_HOLD_PIN, HIGH)` を `setup()` 冒頭（`delay()` より前）に置き、C1 の遅延時間内に自己保持を確立する

**目的**: DeepSleep では ESP32 の消費はほぼゼロになるが LM2596S は動き続けるため、ボディダイオード経由の電流パスが残る。完全電源断によって 12V バスへの消費を完全に止め、main バッテリーへの影響をゼロにする。

**前提**: アラートが機能すれば main 12V 以下になる前に人が対処できる。この回路はアラートが届かなかった最悪ケースへの安全ネット。製造コスト上の制約から、BJT 化（ボディダイオード解消）と同一ロットで発注する。

### TODO: SIM7080G AT コマンドの書き込み検証（コマンド化け対策・未着手）

参考: [necobit/UWB-module-test の sleep-recovery.md](https://github.com/necobit/UWB-module-test/blob/master/docs/sleep-recovery.md)。
REYAX RYUW122（UWBモジュール）で `AT+MODE=1` を送ったつもりが UART のビット化けで `AT+MODE=2`（スリープ）として受理され、
フラッシュに永続化されて再起動後も無応答（文鎮化したように見える）になった事例。予防策として「読んでから書く（既に正しい値なら書き込みを省略）」
「書いたら `?` 照会で読み戻して検証する」の2点が紹介されている。

`device/lte.cpp` の `Lte::setup()` にある `sendCmd("AT+CSCLK=0")`（[lte.cpp:358](esp32_iot_gateway/src/device/lte.cpp#L358)、スリープ無効化）が同じ構造のリスクを持つ。
`AT+CFUN` や `AT+CGDCONT` は後続の接続確認・SMS Ready 待ちが間接的な検証になっているが、CSCLK には化けを検知する後段のチェックが一切ない。
化けて `AT+CSCLK=1`/`2` のまま通ると、走行中に SIM7080G が勝手にスリープして UART 無応答になり得る（原因究明が難しい）。

**実装方針（案）**:

- `Lte` に検証付き設定用のメソッドを追加（例: `ensureSleepDisabled()`）
- `AT+CSCLK?` で現在値を照会 → 既に `+CSCLK: 0` ならスキップ
- 異なれば `AT+CSCLK=0` を送信 → 再度 `AT+CSCLK?` で読み戻し確認
- 一定回数（例: 3回）試行しても検証できなければログに警告を残す（現状ログのみ、リカバリー動作は未検討）

**スコープ**: 他のコマンド（`AT+CGDCONT` 等）への拡張は、CSCLK での効果を見てから検討する。

### TODO: mobileアプリのrelease署名を専用keystoreに切り替え（未着手）

`mobile/android/app/build.gradle:36-41` の `release` ビルドタイプが `signingConfigs.getByName("debug")` を使っている（`flutter run --release` を通すための暫定対応、コード中のTODOコメント参照）。debugキーストア（`%USERPROFILE%\.android\debug.keystore`）はPCごとに自動生成される使い捨ての鍵のため、別PCでビルドしたAPKは署名が一致せず、端末への上書きインストール時に `INSTALL_FAILED_UPDATE_INCOMPATIBLE` が発生する（現状はflutter/adbが自動でアンインストール→再インストールするため実害はないが、ローカルデータは消える）。

**実装方針（案）**:

- `keytool` で専用のreleaseキーストアを新規作成
- `key.properties`（`.gitignore` 対象、パス・パスワードを記載）を作成し、`build.gradle` の `signingConfigs.release` から参照する構成に変更
- keystoreファイル自体はGitに含めず、USBやパスワードマネージャー等の安全な方法でPC間共有する

### TODO: developビルドでOTA Jobsが「ジョブなし」を返し続けた原因不明のバグ（未解決・原因不明）

2026-08-07、実車（develop ビルド、fw 1.20.0）に対する1.21.0のOTAジョブが、Restartを4回試しても毎回シリアルログに `[JOBS] ジョブなし` を出力して失敗し続けた。同一デバイスに `v1-release` env でビルドしたファームを書き込んだところ、初回起動で即座にジョブを受信し、AWS IoT Jobs側も成功（SUCCEEDED）した。

**確認して除外した原因**:

- AWS側のIoT Jobs Publish/Subscribe用IAMポリシー（`infra/iot.tf`）: `terraform plan` で差分なし、適用済みと確認
- Thing Groupのターゲット対象: ジョブの「ターゲット」一覧にこのデバイスが含まれていることを確認済み
- `getDeviceId()`（MACアドレスから `esp32-gw-{mac}` を生成、`config.cpp:17-28`）: 決定的な処理で develop/release 間で値が変わる要素がない
- `DEBUG_MODE` マクロ: `main.cpp:59-63` の動作モード初期値以外どこにも影響しないことを確認済み（`jobsGetNext()`/`mqtt.cpp`/`getDeviceId()`のいずれも参照していない）
- ログが `[JOBS] レスポンスなし`（タイムアウト）ではなく `[JOBS] ジョブなし`（`exec.isNull()`）だったため、MQTT subscribe自体は成功しAWSからの正規応答は受信できていた

**未解決**: develop/releaseのコード差分は実質 `DEBUG_MODE` の有無（Jobs関連コードに無関係と確認済み）と最適化レベル（`-Os`有無、release env）のみで、`jobsGetNext()`自体のロジックは共通。にもかかわらず何が結果を変えたのか特定できていない。「developのまま再度Restartしたら直る可能性」も検証できておらず、develop固有の問題なのか、単なるタイミング/AWS側の一時的な状態だったのかも未確定（後者を裏付ける根拠はなく、憶測に留まる）。

**次にできること**: 次回developビルドで同様の事象が起きたら、`pio device monitor`でログを見ながら同じdevelopビルドのまま複数回Restartして再現するか確認する。再現すれば develop 固有（`-Os`有無等のビルド差）を疑う根拠になる。

### TODO: SPIFFSログ永続化が既知の未解決問題により無効化中（原因未特定・対応は無効化のみ）

`logStorageWrite()`（[log_storage.cpp:98](esp32_iot_gateway/src/service/log_storage.cpp#L98)）の`SPIFFS.open(s_currentPath, FILE_APPEND)`が、`logStorageInit()`で一度`FILE_WRITE`モードで作成・closeした直後から毎回失敗し続ける事象を実機で確認（2026-08-09）。`[E][vfs_api.cpp:301] VFSFileImpl(): fopen(...) failed`がSerialに大量出力される。

**調査結果**: ESP-IDF/arduino-esp32双方のリポジトリに同一症状の未解決issueが複数存在する（[esp-idf#1012](https://github.com/espressif/esp-idf/issues/1012) "File append not working with SPIFFS"、[esp-idf#9915](https://github.com/espressif/esp-idf/issues/9915)、[arduino-esp32#5250](https://github.com/espressif/arduino-esp32/issues/5250)）が、いずれも`Resolution: More info needed`のまま未解決で、根本原因（`spiffs_open()`内部の何が失敗しているか）は特定できていない。`vfs_api.cpp`のソース自体にもappendモード固有の特別扱いは無い。公式SPIFFSドキュメントにはappendモード特有の言及はないが、GC性能・電源断での破損リスク・利用効率75%程度という一般的な脆弱性は明記されている。

**影響範囲（重要）**: この問題により`logStorageWrite()`は事実上ずっと機能しておらず、SPIFFS上のデバッグログファイルへの永続化が全て失敗していた可能性がある。シリアルモニタ接続時は`[E]`ログで気づけるが、実運用中（車載・モニタなし）は完全に不可視。**過去にSPIFFS上のログファイルを根拠にした調査結果があれば信頼できない**（他のTODOでは幸い`pio device monitor`でのシリアル直接確認を使っており、この問題の影響は受けていない）。

**対応**: 原因が未解明のため修正は行わず、ビルドオプションで無効化した。`platformio.ini`の実機4env（v1/v2 × release/develop）に`-D LOG_STORAGE_DISABLED`を追加し、`log_storage.cpp`を`#ifdef LOG_STORAGE_DISABLED`で分岐させて`logStorageInit()`/`logStorageWrite()`/`logStorageClear()`全てをスタブ化した（元の実装は`#else`側にそのまま残してあり、削除していない）。`getDebugLogEnabled()`/Shadowの`debug_log`設定自体は残るが、現状は効果を持たない。原因が判明・修正されたら`platformio.ini`から`LOG_STORAGE_DISABLED`を外すだけで復元できる。

### TODO: OBDトリップのAIナレーティブ生成（未着手、設計メモのみ）

`infra/lambda_src/trip_analysis/index.py`の`_save_trip()`が書き出すトリップJSONには`narrative`フィールドが将来のBedrock連携用に空文字で予約済み（PR #35）。実装はまだだが、プロンプト設計について検討した内容を残す。

**渡すデータの方針**:

- 既存の集計値（`_compute_summary()`が返す`distance_km`/`duration_sec`/`fuel_l`/`fuel_economy_km_l`/`ltft_avg`/`stft_avg`/`catalyst_temp_max`/`boost_kpa_max`/`coolant_start`/`coolant_end`）に加え、10秒バケットごとのmax/min/meanを一部項目で渡すと精度が上がりそう
- 全項目に4統計を乗せるとトークン量が膨らむため、**変動が速い項目だけ**バケット統計（max/min/mean。medianは10秒粒度だとmeanとの差が小さそうなので保留）にし、`coolant_c`のようにゆっくり変化する項目は平均のみでよい
- バケット統計を乗せる対象（変動が速い項目）: `rpm` / `boost_kpa` / `throttle_pct` / `speed_kmh` / `ltft_pct` / `stft_pct` / `timing_deg`（点火時期、急な遅角でノッキング兆候を検知） / `iat_c`（吸気温、`boost_kpa`と組み合わせるとインタークーラーの熱ダレが見える）
- 単純な平均・スカラー値でよい項目: `coolant_c`、`ecu_voltage`（オルタネータ/バッテリー異常の文脈、この車載システムが元々バッテリー電圧監視用だった経緯があるため）
- 保留（面白いが実装コストが上がるので後回し）: `o2_s1_ratio`（実測AFR）と`commanded_afr`（指令AFR）の乖離比較、`accel_pedal_d_pct`（生ペダル開度）と`throttle_pct`（実スロットル）の乖離比較
- 検討したが対象外: `evap_purge_pct`（パージバルブ指令率、通常走行の診断とは関係が薄い）、位置情報`lat`/`lon`（ルート based narrative は複雑さ・プライバシー面で保留）
- エアコン稼働状況は現状取得不可（`esp32_iot_gateway/OBD.md`参照。Honda N-VAN実車検証でOBD-IIポートからは生F-CANも見えず、Mode 22 UDS探索が必要。DID候補は未確認）

**プロンプト出力の方針（案）**: ①一言サマリー ②しきい値付きの異常値指摘（LTFT/STFT±10%、触媒温度上限、ブースト圧の想定レンジなど車種依存の閾値をプロンプト側に明記） ③非エンジニア向けの平易な説明

**実装方針（未確定）**: `_process_job()`内でトリップのrowsを10秒バケットに分割し、上記項目ごとにmax/min/mean（または平均のみ）を計算してBedrockへ渡す前処理を追加する想定。Bedrockモデル選定・呼び出し方式（同期/非同期）・コストは未検討。

**実機データでの試作検証（2026-08-18、ローカル一時スクリプトで検証、実装には未反映）**:

- 実際のトリップ3件（`car-iot-aca704`、Bedrock Nova Pro、`apac.amazon.nova-pro-v1:0`）に対し、30秒バケットのCSV時系列＋集計値でプロンプトを組んで試したところ、末尾トリップの点火時期遅角パターンなど集計値だけでは見えない指摘ができることを確認。バケット幅は10秒より30秒の方がトークン効率が良く（同一トリップで入力17,105→6,300トークン）、かつ具体的な数値引用の質は落ちなかった
- **固定しきい値（LTFT/STFT合計±10%）は今回試した3トリップ全てで「異常の可能性」と指摘され、オオカミ少年状態になった**。3トリップの`ltft_avg`/`stft_avg`合計はいずれも-10.3%〜-12.9%で、これがこの車の平常運転時の値域である可能性が高い。**固定の一般的しきい値をプロンプトに書く方式は見直し、直近N トリップの平均からの乖離で判定する車固有ベースライン方式に変更した方がよさそう**（例: 「この車の直近トリップの平均LTFT/STFTは約-11%、今回はそこから大きく外れていない/外れている」という形でプロンプトに渡す）
- 燃費についても同様の考え方が要る。一般的な軽ターボ車の目安（15〜20km/L）をプロンプトに書いて比較させると3トリップとも「低い」と評価されたが、これも距離5km前後の短距離トリップ限定のサンプルなので、この車の短距離トリップ平均と比較する方が公平な可能性がある
- 検証に使ったローカルスクリプト（`bucket_trip.py`／`bucket_and_prompt.py`、S3のOBD生データをndjsonのまま読んでバケット集計しBedrock Converse APIへ投げる）はリポジトリには未コミット（一時検証用、スクラッチパッドに作成）
- Bedrock呼び出し時のハマりどころ: Windows上のAWS CLIで日本語を含むBedrockレスポンスを扱う際は`PYTHONIOENCODING=utf-8`を付けないと出力が文字化けする。Anthropicモデル（Claude Haiku 4.5等）は初回利用時にBedrockコンソールの「モデルアクセス」からAnthropic use case detailsフォームの提出が必要（Amazon純正のNovaシリーズはこのゲート対象外）

**位置情報（緯度経度）の活用も試作検証した（2026-08-18）**:

- **LLMに生の緯度経度を渡して地名を推測させるのは避ける**（座標→地名の変換はモデルの内部知識頼みになり、特に住宅地ほど誤った地名を自信満々に返すハルシネーションのリスクが高い）。代わりにAWS Location Serviceで逆ジオコーディングし、実データとして得た地名文字列をプロンプトに渡す方式にした
- **データソースはEsriではなくHEREを使う**。同じAWS Location Serviceでも、Esriは日本のPOI検索がほぼ機能しない（「駐車場」で検索したら140km離れた無関係な地名がヒットした）のに対し、HEREは近隣の実在施設名（コンビニ名・工場名等）まで正確に距離順で返す
- **自宅判定**: 頻繁に訪れる自宅のような地点は、固定の緯度経度＋半径（試作では50m、haversine距離で判定）から「自宅」と判定し、逆ジオコーディングを呼ばずに済ませる設計にした。これにより(a)プロンプト上で住所を露出しない(b)API呼び出しを削減、の両方のメリットがある。将来実装する場合、自宅座標は環境変数等の設定値にする想定（ハードコードしない）
- 近隣POIをプロンプトに渡す場合は「〜付近」「近隣の目印」という参考情報の扱いであることを明記し、「そこを訪れた」と断定させないようプロンプト側で釘を刺す必要がある（半径100m圏内の検索結果であって、車が実際にその施設に立ち寄ったことを意味しないため）
- **「フィールドごとの基準値をプロンプトで与えないと、そのフィールド単体でハルシネーション気味の指摘をされる」という問題が、ECU電圧（前回確認）に続き`boost_kpa`でも再現した**: ブースト圧の基準値を一切渡していないのに、モデルが「正常範囲は一般的に30〜60kPa程度」という架空の数値を作り出し、実測90kPaを「エンジンに過度の負荷」と誤って指摘した。**実装時は、プロンプトに含める数値項目それぞれに対して「この値域は正常」という参考情報（車固有ベースライン、またはせめて一般的な目安）を漏れなく添える必要がある**。当面の対象は`ltft_pct`/`stft_pct`（対応済み）、`ecu_voltage`（対応済み）、`boost_kpa`（未対応）、`catalyst_temp_c`（未検証）、`timing_deg`（未検証）

**車固有ベースラインの具体的な算出方法も試作検証した（2026-08-18）**:

- ユーザー提案: 間引き時に「走行中の平均値」を`row_count`（サンプル数）とともにトリップJSONへ保存し、複数トリップの`row_count`重みつき平均で長期間のベースラインを算出してAIへの参考情報として渡す
- 実際の17トリップの`ltft_avg`/`stft_avg`で試したところ、単純な「トリップ平均の平均」と`row_count`重みつき平均で無視できない差が出た（例: `stft_avg`は単純平均-4.22%に対し重みつき平均-4.88%）。**サンプル数が少ない短時間トリップが単純平均だと過大に影響してしまうため、`row_count`による重みづけは必須**
- **さらに大きな問題を発見**: 17トリップ中12件が「`distance_km`≈0なのに`duration_sec`・`fuel_l`は正常値」という、GPS未捕捉（屋内駐車・コールドスタート等）によるものと思われる"幽霊トリップ"だった（例: 2663秒間で燃料1.248L消費しているのに走行距離0.0km）。目下OBD検証作業でアイドリングさせている時間が多いことも一因。これが`fuel_economy_km_l`（`distance_km/fuel_l`）を汚染し、全17トリップの重みつき平均燃費は3.60km/Lだが、`distance_km > 0.5km`の実質的な移動トリップ5件に絞ると9.8km/Lと大きく変わった（3.6と9.8では「今回の燃費は平均並み」と「今回の燃費は平均よりだいぶ良い」で評価が逆転する）
- **設計への反映**: ベースライン計算では`distance_km`が一定以上（例0.5km）のトリップのみを対象にする距離フィルタが必須、特に燃費など「距離が分母」の指標では影響が大きい。LTFT/STFTのような距離に依存しない指標への影響は小さいが、統一して「実質的な移動があったトリップ」だけをベースライン対象にする方が安全
- `boost_kpa`をベースライン化するには、現状`_compute_summary()`が`boost_kpa_max`しか返していないため`boost_kpa_avg`の追加が必要（実装時のTODO）

**「好条件区間」の算出方法も試作検証した（2026-08-18、数値は全てサンプル・参考程度。データが増えるほど変わるので固定値として実装に埋め込まない）**:

- 1行ごとの瞬間燃費（`speed_kmh/fuel_rate_lph`）は**ノイズが大きすぎて使い物にならない**。コースティング等で`fuel_rate_lph`が一瞬ゼロ近くまで下がる区間を拾うと80km/L超のような非現実的な値が出る
- 代わりに**`_compute_summary()`と同じ「距離はhaversine積算・燃料はfuel_rate_lphの台形積分」方式を、トリップ全体ではなく1〜3分程度のスライディングウィンドウに適用する**と、現実的で安定した区間平均が得られる（試作では60/120/180秒窓を試した）
- この方式で「良い区間」を抽出すると、3トリップ共通で似た値域に収束することを確認（あくまでサンプル値）: 速度16〜31km/h程度・rpm1300〜1750程度・throttle16〜21%程度・boost_kpaが-35〜-45kPa程度（強めの負圧＝軽負荷）・load_pct28〜30%程度の「軽負荷での定速巡航」区間
- **設計への示唆**: 将来、トリップ内で「最も効率が良かった区間の走行条件」を車固有の実績として抽出し、他のトリップの平均的な走行条件と比較する（＝もう一種類のベースライン）という使い方ができそう。ただし具体的な数値しきい値をコードに埋め込むのではなく、都度実データから計算する設計にすること（今回出た数値はあくまで検証時点のサンプル）

**ベースライン統合テスト＆z-score方式の検証（2026-08-18）**: ここまでの知見（30秒バケット時系列＋位置情報＋車固有ベースライン＋好条件区間）を全部載せたプロンプトでtrip1を再分析させたところ、新たな問題が見つかった。

- プロンプトに`boost_kpa`の平均値（約-32.2kPa）と典型的な最大値（約92kPa）の**両方**を渡したところ、AIは今回のトリップの**最大値**（90kPa）を、ベースラインの**平均値**（-32.2kPa）と比較して「ベースラインを大きく上回る」と誤って指摘した。本来比較すべきは「今回の最大値」と「ベースラインの典型的な最大値」（90 vs 92、ほぼ同じ＝正常）であり、平均と最大という異なる統計量を人間なら混同しないが、AIは同種の「参考値」として並んでいると取り違えるリスクがある
- **これを受けて、ベースライン算出時には平均だけでなく標準偏差も計算し、z-score（`(今回の値 - 平均) / 標準偏差`）で機械的に判定する方式を試した**。`boost_kpa_max`について実際の17トリップ（`row_count`重みつき平均・標準偏差）で計算すると、移動実績のある5トリップに絞っても全17トリップで見ても平均78.8kPa・標準偏差15.4kPa前後になり、trip1の90kPaはz≈0.73（1標準偏差以内）で明確に正常範囲だと機械的に判定できた。AIの「大きく上回る」という誤判定を、コード側の統計計算で明確に反証できた
- **設計への反映（重要）**: 「数値の大小判定」はAIに委ねず、**ベースライン算出時に平均・標準偏差（row_count重みつき）を計算しておき、z-scoreの計算はコード側で行う**。AIには生の比較対象2つを渡して判断させるのではなく、`{{"field": "boost_kpa_max", "value": 90, "baseline_mean": 78.8, "baseline_std": 15.4, "z_score": 0.73, "judgement": "正常範囲内"}}`のような**判定済みの結果**を渡し、AIの役割は「その判定結果を人間向けの自然な文章にする」ことに限定するのが安全（`|z|>2`程度を「要注意」の目安にする等、具体的な閾値は実装時に検討）。数値比較・異常判定という決定的な処理は、これまでの検証で繰り返しAIが誤りやすいと分かった部分であり、コード側に持たせるべき

**時系列データへの偏差値付与も試作検証した（2026-08-18）**: 30秒バケットの時系列CSVに、項目ごとの
偏差値（`50 + 10 × (値 - 母集団平均) / 母集団標準偏差`、母集団はtrip1を含まない他トリップのrow単位
データ）を付与し、「偏差値30〜70は通常範囲、それ以外だけ指摘して」と明示してtrip1を再分析させた。

- **全項目に付けるのではなく、「正常/異常」の概念になじむ項目だけに絞った**: `ltft_pct`/`stft_pct`
  （燃調異常の指標）・`boost_kpa`（過給圧の異常検知）・`timing_deg`（急な遅角=ノッキング検知）には
  偏差値を付与。`rpm`/`throttle_pct`/`speed_kmh`はドライバーの操作・状況（渋滞/信号等）に強く依存し
  値そのものに「正常/異常」の概念が薄いため対象外。`iat_c`も瞬間値の逸脱よりトレンド（走行中の
  上昇傾向）の方が意味を持つため対象外とした
- **効果は大きかった。このシリーズで初めて、全項目「正常範囲内」と誤検知ゼロで判定できた**
  （それまではboost_kpa・ECU電圧等、毎回どこかしら誤検知していた）。偏差値という明確な数値レンジ
  （30〜70）を与えたことで、AIの「なんとなく異常っぽい」という曖昧な直感的判断を抑制できた
- **残った問題**: 偏差値の数値自体は正しく引用できていたが、それを自然文で説明する際に**方向を
  取り違えるケースがあった**（LTFTの偏差値59〜67＝平均より高い、を「平均より若干低い傾向」と誤って
  説明した。LTFTは値がマイナスに大きいほど「補正が濃い」という符号の解釈が絡み、そこで混乱したと
  見られる）。数値の異常判定（z-score/偏差値の計算）はコード側に持たせることで解決したが、
  「その数値が何を意味するか」の自然文への翻訳では依然としてAI側の誤りが残りうる
- **ユーザーフィードバック（重要）**: 生成された説明文が「偏差値」という単語を多用しすぎていた。
  **偏差値はAIが内部で「これは通常範囲か」を判定するための道具として使うべきで、人間向けの説明文
  では偏差値という言葉自体を持ち出さず、もっと自然な言葉（「いつもより少し」「ほぼ変わらない」等）
  で表現させるようプロンプトを調整する必要がある**

**残課題2件を解消する改善策を試作・検証し、両方とも効果を確認した（2026-08-18）**:

- **対策**: 偏差値の生の数値をAIに渡すのをやめ、**項目ごとに「値が大きい方向/小さい方向が何を
  意味するか」を事前にコード側でマッピングしておき、偏差値の帯（大きく外れ/やや外れ/通常どおり）
  と組み合わせて完成済みの自然言語ラベルに変換してから渡す**方式にした。例:
  `FIELD_LABELS = {{"ltft_pct": {{"high": "薄め", "low": "濃いめ"}}, "boost_kpa": {{"high": "高め", "low": "低め"}}, ...}}`
  というマッピングを用意し、偏差値70以上なら「大きく{{high}}」、60〜70なら「やや{{high}}」、
  30〜40なら「やや{{low}}」、30未満なら「大きく{{low}}」、それ以外は「通常どおり」という
  ラベルを算出してCSVの列に入れる（生の偏差値の数値は渡さない）。加えてプロンプトに
  「統計用語（偏差値・標準偏差・z-score等）は一切使わず自然な日本語で表現してください」
  「ラベルは既に判定済みの結果なので数値を独自解釈しないでください」を明記した
- **効果**: 両方の問題が解消した。①方向の取り違えが起きなくなった（LTFTの「大きく薄め」判定が、
  実際に偏差値が高かった区間と正しく対応するようになった）。②説明文に「偏差値」「標準偏差」
  「z-score」等の統計用語は一切登場せず、ラベル文言（「やや薄め」「大きく薄め」等）をそのまま
  自然に文中へ引用する書き方になった。「やや」と「大きく」の重要度の違いもきちんと書き分けられ、
  「大きく」判定の箇所だけを注意点として明確に指摘し、「やや」判定は軽いトーンに留めていた
- **最終的な設計方針（確定）**: AIに渡す数値は「生の値」ではなく「コード側で判定・ラベル化済みの
  自然言語」にする。数値の大小判定・方向の意味づけ・異常/正常の判定は全てコード側の責務とし、
  AIの役割は「ラベルを人間向けの自然な文章に織り込むこと」に限定する。これは本セッションを通じて
  繰り返し確認された教訓（固定しきい値のオオカミ少年化、基準値未提示によるハルシネーション、
  平均/最大の取り違え、方向の取り違え）に対する統一的な解決策になっている

**ラベルの「意味を踏まえた分析」と「サマリーの粒度」も追加で調整した（2026-08-18）**:

- **ユーザーフィードバック①**: ラベル（「やや薄め」等）を単なるタグとして文中に繰り返すだけでは
  不十分で、**そのラベルが物理的に何を意味するかを踏まえた分析をしてほしい**という要望。対策として、
  プロンプトにラベルごとの物理的な意味（例:「薄め」＝燃料噴射を減らす方向の補正、軽負荷巡航では
  自然に起こりうるが継続的だと吸気系の微小な空気漏れ込み等が背景のこともある一般的傾向）を明記し、
  「ラベルを単なるタグとして繰り返すのではなく、この意味を踏まえて説明に組み込むこと」「ただし
  この車固有の根本原因を断定するのは避け、一般的に起こりうる背景の説明に留めること」を指示した。
  効果はあり、「燃料噴射を減らす方向に補正されている状態が継続していることを示しており、吸気系の
  わずかな空気の漏れ込み等が背景にある可能性があります」のように、意味を汲んだ説明ができるように
  なった
- **ユーザーフィードバック②**: 一言サマリーが冷却水温・ECU電圧等の数値を細かく列挙しすぎていた。
  **燃費・消費燃料の数値は残しつつ、それ以外はアバウトな一言（「エンジン・電気系統は普段どおり」等）
  に留めるよう**プロンプトで明示したところ、意図通りの粒度になった
- **ユーザーフィードバック③**: ①の対策後、モデルが与えた意味の説明を超えて独自の一般知識ベースの
  注意喚起（例:「極端な進角や遅角はエンジンの寿命に影響を与える可能性があります」）を付け加える
  ようになった。**「今回のデータから実際に読み取れた事実の報告を優先し、そこから発展した一般論的な
  警告・懸念表明は最小限にしてください」「ラベルの意味の説明は与えた範囲に留め、そこから独自に
  一般知識に基づく注意喚起を付け加えないでください」を追加で明記したところ解消**し、「継続的に
  『薄め』の傾向が続いている点には注意が必要です」のような、データに基づく事実ベースの指摘に
  なった。①の「意味を踏まえた分析をさせる」指示と③の「その意味の説明範囲を超えさせない」指示は
  セットで機能させる必要がある点に注意（①だけだとモデルが意味の説明から憶測を膨らませがち）

**時系列データの参照を「秒数」から「座標マーカー」に変更する試作も行った（2026-08-18）**:

- **ユーザーフィードバック**: レポート中の「1950秒付近では〜」のような秒数への言及は、人間には
  何が起きた瞬間なのか分からず意味を持たない。後から機械的に地図情報と紐づけて参照できるよう、
  秒数の代わりに`{{lat:xxx,lon:yyy}}`形式の座標マーカーを出力させたい、という要望
- **対策**: バケット化のCSVから`t_sec`列を削除し、各バケットの代表座標（先頭行の`lat`/`lon`、
  30秒程度の粒度ならGPS精度誤差の範囲内で十分な代表値）を`lat`/`lon`列として持たせた。プロンプトには
  「秒数表現は使わず、該当行の`lat`/`lon`をそのまま使って`{{lat:35.7931576,lon:139.5804419}}`形式で
  言及すること」「値は丸めずそのまま使うこと」を明記
- **効果**: 狙い通り機能した。生成されたレポート中の座標マーカー（例:
  `{{lat:35.7666657,lon:139.6229164}}`）は、実際にCSVの該当行の値と完全に一致しており（丸め・改変なし）、
  かつ実際に「大きく薄め」だった区間を正しく指し示していた
- **残った問題（再発）**: v3で対策したはずの「一般知識ベースの過剰な注意喚起」が一部再発し、
  末尾に「定期的な点検がおすすめです」という提案的な一文が付いた。「〜はエンジンの寿命に影響します」
  のような明確な警告文言とは違い、「点検がおすすめ」は提案寄りの言い回しのため、「警告・懸念表明を
  最小限に」という指示に抵触すると判定されなかった可能性がある。また、座標マーカーの指示を追加して
  プロンプトが長くなった（約7.6千字→8.8千字）ことで、他の指示の効きがわずかに弱まった可能性も
  ある。**プロンプトが長くなるほど個々の指示の遵守率が下がりうる点は、今後プロンプトに項目を
  追加していく際に注意が必要**（生成のばらつきの範囲内である可能性もあり、複数回の実行で
  再現性を確認すべき）

**エッジケース（アイドリング専用トリップ・短距離トリップ）でのテストで、重大なハルシネーションを
発見した（2026-08-18）**:

- 「アイドリングしかしていないトリップ」（distance_km=0、GPSデータが一切記録されていない。
  屋内駐車中のOBDテストと推測される）と「距離0.42km・188秒のみの短距離トリップ」（全30秒バケット・
  全項目が「通常どおり」）の2件で試した
- **短距離トリップで重大な問題が見つかった**: 全バケット・全項目「通常どおり」だったにもかかわらず、
  AIは「②注意すべき数値の指摘」に**実際には存在しない「点火時期がやや遅角気味」というラベルを
  捏造した**。座標マーカー自体（`{{lat:35.7637985,lon:139.6260201}}`）はCSVの実在行から正確に
  コピーされていたが、その行の`timing_deg_level`は実際には「通常どおり」であり、存在しない
  ラベルと実在の座標を組み合わせて言及していた。これは今までの「解釈を誤る」「一般論を付け足す」
  とは質的に異なる、**データに存在しない内容の捏造**であり、最も深刻な問題として扱うべき
- **原因の推測**: プロンプト構成が「②注意すべき数値の指摘」を独立した必須項目として要求していたため、
  実際には指摘すべきことが何もない場合でも、AIが項目を埋めようとする圧力から何かを作り出したと
  見られる
- **対策**: 「②は、CSV中に実際に『やや◯◯』『大きく◯◯』ラベルが1つ以上存在する場合のみ書く。
  全て『通常どおり』の場合は無理に指摘を作らず『今回のデータでは特筆すべき数値の逸脱はありません
  でした』と正直に書く」「座標マーカーを使う場合は、必ず同じ行にあるラベル・数値だけを紐づける」
  を明記したところ、捏造は解消され「今回のデータでは特筆すべき数値の逸脱はありませんでした」と
  正直に書けるようになった（座標マーカー自体も、紐づけるべきラベルが無かったため使われなくなった）
- **教訓（重要）**: レポートのフォーマットが「必ず◯項目分書く」という構造を強制すると、AIは
  書くことが無い場合でも何かを捏造してでも埋めようとする傾向がある。**プロンプト側で「該当が
  無ければ『特筆事項なし』と正直に書いてよい」ことを明示的に許可しておく必要がある**。これは
  項目数が増えるほど（今回のように①②③のような固定フォーマットを要求するほど）起こりやすい
  リスクだと考えられる
- **未解消の問題**: 「一般論的な提案（『点検がおすすめ』等）を最小限にする」指示は、この
  エッジケーステストでも完全には効かず、アイドリングトリップの回答で「継続的なモニタリングは
  推奨されます」、短距離トリップの回答（対策後）でも「点検が必要かもしれません」という提案文が
  それぞれ残った。この指示は複数回試しても100%は効かない、比較的頑固な問題として認識しておく

**検証方法自体の誤りにも気づいた（2026-08-18、ユーザー指摘）**: 上記の`dist_txt`に
「GPSデータが一切記録されておらず、屋内駐車中など測位できない環境だったと推測されます」という
一文を含めていたが、**これはClaude（検証作業者）が自分で解釈して書き加えた文章であり、本番の
機械的な処理では出てこないデータ**。本番はコード側の集計結果だけをAIに渡す設計のはずなのに、
検証プロンプトにAI的な解釈を事前に混入させてしまっていた。「走行距離: 0.00 km（GPSデータが
記録されていません）」という事実のみに修正して再実行した。**プロンプトの試作・検証を行う際は、
渡すデータが実際に機械的な処理だけで作れる内容になっているか、意識的にチェックする必要がある**
（渡すデータそのものに検証者の解釈が紛れ込むと、AIの出力が良く見えても本番では再現できない）。

修正後の再実行で新たに見つかった問題:

- **空の座標マーカー**: GPSデータが一切ないアイドリングトリップに対し、AIは`{{lat:,lon:}}`という
  **値が空のマーカーを3箇所生成**した。「lat/lonが空欄の行は座標マーカーを使わず『GPS未捕捉の
  時間帯』と表現すること」という指示があったにもかかわらず、それに従わず空マーカーを機械的に
  出力してしまった。下流で地図に紐づけようとすると無意味なデータになるため、実装時は「空マーカーが
  出力された場合は表示しない」等のガード処理をAI出力の後段（コード側）でも用意しておくべき
- **①一言サマリーの粒度逸脱**: 燃費が算出不可（距離0のため）だったケースで、AIが代わりに
  「冷却水温は67°Cから91°Cに上昇しました」と数値を持ち出し、「燃費・消費燃料以外は数値を
  列挙せずアバウトに」という指示から外れた。燃費という主役の数値が無い時、AIが手持ちの別の
  数値で埋め合わせようとする傾向があるかもしれない
- 一方、短距離トリップは修正後も引き続き良好（捏造なし、正直な「特筆事項なし」の報告）だった

**「提案の隔離」と「燃費の二軸評価」を試作・検証し、両方とも効果を確認した（2026-08-18）**:

- **ユーザーフィードバック①**: これまでの検証で繰り返し「点検がおすすめです」「モニタリングを
  推奨します」のような提案的な一文が②③の本文中に散発的に混ざり込み、指示で抑えようとしても
  完全には消えなかった。**対策として、レポート構成に「④提案」を独立した項目として追加し、
  提案・アドバイスに類する文言は④にのみ書かせ、①②③本文には一切書かせないよう明示した**。
  さらに④自体も「②で『大きく』判定の項目があった場合のみ書く、無ければ『特にありません』」と
  条件付けた。効果を確認: ④に提案が集約され、①②③の本文には「〜がおすすめです」等の文言が
  一切現れなくなった
- **ユーザーフィードバック②**: 燃費9.03km/Lは、素人目線では「悪めだね」という直感的な印象を
  持つはずだが、車固有ベースライン（この車の実績としては普段どおり、n=5の少数実績）とだけ
  比較していたため、その素朴な感覚がレポートに反映されていなかった。**対策として、車固有の
  ベースライン比較に加えて「一般的な軽ターボ車の実燃費目安（15〜20km/L程度、車種依存のため
  参考程度）」との比較も復活させ、両方をコード側で機械的に評価（数値の大小判定はAIに委ねない
  既存方針を踏襲）してから、判定済みの評価文としてプロンプトに渡した**。効果を確認: 「この車の
  実績としては普段どおりですが、一般的な軽ターボ車の目安を下回っています」のように、車固有の
  文脈と一般的な感覚の両方を反映した評価がレポートに現れるようになった
- **教訓**: 「この車固有のベースラインと比較する」方式は固定しきい値のオオカミ少年化を防ぐ効果は
  あるが、**それだけでは人間の素朴な絶対評価の感覚（「そもそも一般的に見てどうなのか」）が
  抜け落ちる**。車固有の相対評価と一般的な絶対評価は補完関係にあり、両方をコード側で計算して
  渡すのが良さそう
