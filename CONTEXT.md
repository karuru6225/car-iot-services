# プロジェクトコンテキスト

## 概要

SwitchBot 防水温湿度計（WoIOSensor）の BLE アドバタイズを M5Atom S3 でスキャンし、
温度・湿度・バッテリーを Serial および内蔵ディスプレイに表示するスケッチ。
将来的に LTE（SORACOM Cat-M）でサーバーへのデータ送信を追加予定。

## ハードウェア

| 項目 | 内容 |
|---|---|
| MCU | M5Atom S3（ESP32-S3） |
| ディスプレイ | 内蔵 0.85" IPS LCD 128×128（GC9107） |
| ボタン | GPIO 41（Active-LOW、ハードウェアプルアップ内蔵） |
| センサー | SwitchBot WoIOSensor（BLE アドバタイズのみ、接続不要） |
| 電源 | 12V バッテリー → DC-DC / レギュレータ → M5Atom S3 |
| LTE（予定） | M5Stack U128（SIM7080G CAT-M/NB-IoT）、SORACOM Cat-M SIM |

### ADC ピン（外部アクセス可能）

| GPIO | ADC ch | 用途 |
|---|---|---|
| G7 | ACH6 | バッテリー監視①（12V 系 分圧入力）予定 |
| G8 | ACH7 | バッテリー監視②（外部バッテリー 分圧入力）予定 |
| G5/G6 | ACH4/5 | 将来の拡張用 |

全ピン ADC1（GPIO 1〜10）のため BLE 使用中も利用可能。

## 動作モード

### Deep Sleep モード（デフォルト）

1. BLE スキャン（5 秒）
2. 登録済みアドレスのデータのみキューに投入
3. 表示（約 3 秒）
4. `M5.Display.sleep()` → `esp_deep_sleep_start()`
5. `SLEEP_INTERVAL_SEC` 秒後にタイマー復帰 → `setup()` から再実行

### 通常モード（起動時ボタン押下）

1. BLE スキャン（5 秒）
2. 登録済みアドレスのデータのみキューに投入
3. 表示後 10 秒待機（待機中もボタン監視）
4. 繰り返し

### 登録モード（通常モード中に長押しで起動）

1. BLE スキャンで SwitchBot デバイスを検索
2. 見つかったデバイス一覧を表示（登録済み / 未登録を区別）
3. 短押し: 次の項目へ
4. 長押し: 選択中のデバイスを登録（未登録）または削除（登録済）、または戻る

## ファイル構成

```
m5stamp_lite_test01/
├── m5stamp_lite_test01.ino   Application: モード管理・メインループ
├── config.h                  定数（スキャン時間・ピン番号・閾値等）
├── register_mode.h/.cpp      Application Service: 登録ユースケース
├── view.h / view.cpp         Presentation: Serial + Display 出力
├── domain__switchbot_data.h/.cpp  Value Object: センサーデータ・パース
├── domain__targets.h/.cpp         Aggregate + Repository: 監視対象リスト
├── infra__ble_scan.h/.cpp         Infrastructure: BLE スキャン
├── infra__button.h/.cpp           Infrastructure: GPIO ボタン
├── ARCHITECTURE.md           アーキテクチャ・命名規則ドキュメント
├── CONTEXT.md                引き継ぎコンテキスト（本ファイル）
└── LTE_IMPL_PLAN.md          LTE 実装計画（Unit Cat-M / SIM7080G）
```

## 主要クラスとインスタンス

| インスタンス | 型 | 役割 |
|---|---|---|
| `targets` | `Targets` | 監視対象 BLE アドレスの管理・NVS 読み書き |
| `scanner` | `BleScanner` | BLE スキャン・FreeRTOS キュー管理 |
| `button` | `Button` | ISR ベースのボタンイベント検出 |
| `regMode` | `RegisterMode` | 登録モードのロジック |
| `view` | `View` | 出力（`MultiOutput MOut` を内部に持つ） |

## 重要な設計決定

### MultiOutput（Serial + Display 同時出力）
`view.cpp` 内部の `static MultiOutput MOut` が `Print` を継承し、
`write()` で Serial と M5.Display 両方に書き込む。
外部からは `view.sensorData(d)` 等のメソッドのみ見える。

### BLE コールバックと FreeRTOS キュー
`SwitchBotCallback::onResult()` は BLE タスクから呼ばれる。
スレッドセーフな `xQueueSend(scanner.queue, &d, 0)` でメインタスクに渡す。
メインタスクは `xQueueReceive` でキューを消費して表示する。
`scanner.queue` は単一の汎用キュー。誰が消費するかは Application 層の責務。

### ボタン ISR
`btnISR()` は `IRAM_ATTR` 付きの静的関数。
Arduino の制約でメンバ関数にできないため、ファイルスコープの
`static volatile bool sLongFired / sShortFired` で状態を持つ。

### NVS 永続化
`Preferences` ライブラリで namespace `"switchbot"` に保存。
キー `"count"` + `"a0"` ～ `"a9"` で最大 10 件の MAC アドレスを管理。

### SwitchBotData のパース
`SwitchBotData::parse()` が Manufacturer Data / Service Data の生バイト列を
Value Object に変換する責務を持つ。Infrastructure 層には一切パースロジックを置かない。
`raw[]` / `rawLen` フィールドはパース後未使用のため削除済み。

パースフォーマット（WoIOSensor）:
```
Manufacturer Data:
  [0-1]  Company ID (0x0969)
  [2-7]  MAC アドレス
  [10]   温度小数部 (bit3-0)
  [11]   温度整数部 (bit6-0) + 符号 (bit7: 1=正)
  [12]   湿度 (%)
Service Data:
  [2]    バッテリー (%)
```

### include 順序（M5Unified / BLE）
M5Unified を BLE ヘッダより先に include しないとコンフリクトが発生する。
`view.h` が先頭で `<M5Unified.h>` を取り込むため、
`#include "view.h"` を最初に書くことで順序を保証している。

### Deep Sleep モードの起動判定

`setup()` 内で ISR 登録前にボタンをポーリング。

```cpp
gNormalMode = (digitalRead(BTN_PIN) == LOW);
```
`true`（ボタン押下）→ 通常モード、`false` → Deep Sleep モード。

## config.h 定数一覧

| 定数 | 値 | 用途 |
|---|---|---|
| `SWITCHBOT_COMPANY_ID` | `0x0969` | BLE フィルタリング |
| `SCAN_TIME` | `5` | BLE スキャン秒数 |
| `QUEUE_SIZE` | `20` | FreeRTOS キューサイズ |
| `MAX_TARGETS` | `10` | 登録可能デバイス上限 |
| `MAX_FOUND` | `20` | スキャンで見つかるデバイス上限 |
| `BTN_PIN` | `41` | ボタン GPIO 番号 |
| `LONG_PRESS_MS` | `1000` | 長押し判定ミリ秒 |
| `DEBOUNCE_MS` | `50` | チャタリング除去ミリ秒 |
| `SLEEP_INTERVAL_SEC` | `300` | Deep Sleep 間隔（秒） |

## ディスプレイ制約

フォント: `efontJA_12`（ASCII 6px幅、日本語 12px幅）、表示幅 128px

- ASCII のみ: 最大 21 文字/行
- MAC アドレス `XX:XX:XX:XX:XX:XX`: 17 文字 = 102px（収まる）
- 日本語 + ASCII 混在は折り返し発生に注意

## 将来の拡張予定

### LTE 送信（Unit Cat-M / SIM7080G）

詳細は `LTE_IMPL_PLAN.md` を参照。

- `infra__lte.h/.cpp` を追加（`LteModem::enqueue/flush`）
- `flushQueue()` に `lte.enqueue(d)` を追加、`lte.flush()` を呼び出し
- Application 層がファンアウトを担当（BleScanner は変更不要）
- SORACOM Harvest Data（HTTP POST → 201）で送信

### バッテリー電圧監視（ADC）

- G7(ACH6): 12V バッテリー系統（分圧抵抗で 3.3V 以下に降圧）
- G8(ACH7): 外部バッテリー系統
- `domain__battery.h/.cpp` で Value Object 化予定

### マルチコア化

LTE 送信が不安定なことが判明した時点で対応。

- Core 0: BLE scan タスク（常時スキャン）
- Core 1: 表示・ボタン・LTE タスク

### 複数センサー種別への対応（B案 DI）

現在 `SwitchBotData::parse()` は WoIOSensor 専用の static factory。
別の SwitchBot センサーを追加する際は：

1. `IDeviceParser` インターフェースを作成（Domain 層）
2. `WoIOSensorParser : public IDeviceParser` を実装
3. `BleScanner::setup(IDeviceParser& parser)` で注入

## ビルド環境

| 項目 | 内容 |
|---|---|
| IDE | Arduino IDE 2.x / VS Code + Arduino 拡張 |
| ボードパッケージ | M5Stack ESP32 (m5stack) 3.2.5 |
| 主要ライブラリ | M5Unified, ESP32 BLE Arduino, Preferences |
| Arduino制約 | サブディレクトリの .cpp は自動コンパイルされない → 全 .cpp をルートに配置 |
