# アーキテクチャ概要（m5atom_iot_gateway）

> **注意**: このデバイスは段階的廃止予定。アクティブな開発は `esp32_iot_gateway` で行う。

## 層構成

```text
┌─────────────────────────────────────────┐
│          Application 層                  │
│   m5atom_iot_gateway.ino                │
│   app/register_mode.h / .cpp            │
├──────────────────┬──────────────────────┤
│   Domain 層      │   Presentation 層    │
│   domain/*       │   app/view.h / .cpp  │
├──────────────────┴──────────────────────┤
│         Infrastructure 層               │
│  infra/ble_scan, infra/button           │
│  infra/lte, infra/logger               │
├─────────────────────────────────────────┤
│              設定                        │
│           config.h / certs.h            │
└─────────────────────────────────────────┘
```

## ディレクトリ構成

```text
src/
├── m5atom_iot_gateway.ino   Application: メインループ・モード管理
├── config.h                 コンパイル時定数
├── certs.h                  AWS IoT Core 証明書（gitignore）
├── domain/                  Domain 層: Value Object・Repository
│   ├── sensor.h             SensorBase 基底 struct
│   ├── thermometer.h/.cpp   温湿度計データ・パーサー
│   ├── co2meter.h/.cpp      CO2センサーデータ・パーサー
│   ├── sensor_factory.h/.cpp  センサー種別判定・パーサー振り分け
│   └── targets.h/.cpp       監視対象リスト（NVS 永続化）
├── infra/                   Infrastructure 層: ハードウェア・外部アダプター
│   ├── ble_scan.h/.cpp      BLE スキャン・FreeRTOS キュー
│   ├── button.h/.cpp        GPIO ISR ボタン
│   ├── lte.h/.cpp           SIM7080G MQTT over TLS
│   └── logger.h/.cpp        Serial デバッグ出力
└── app/                     Application / Presentation 層
    ├── view.h/.cpp          Serial + Display 出力
    ├── register_mode.h/.cpp 登録ユースケース
    └── bypass_mode.h/.cpp   AT コマンド透過モード（デバッグ用）
```

## 各ファイルの責務

| ファイル | クラス / インスタンス | 責務 |
|---|---|---|
| `domain/sensor.h` | `SensorBase` (struct) | FreeRTOS キュー安全な基底（virtual 禁止） |
| `domain/thermometer` | `ThermometerData`, `ThermometerParser` | 温湿度計データ・パース |
| `domain/co2meter` | `Co2MeterData`, `Co2MeterParser` | CO2センサーデータ・パース |
| `domain/sensor_factory` | `SensorParserFactory` | センサー種別判定・パーサー振り分け |
| `domain/targets` | `Targets targets` | 監視対象アドレスの管理・NVS 永続化 |
| `infra/ble_scan` | `BleScanner scanner` | BLE スキャン・FreeRTOS キュー管理 |
| `infra/button` | `Button button` | GPIO ISR・ボタンイベント管理 |
| `infra/lte` | `Lte lte` | SIM7080G 制御・MQTT over TLS publish |
| `infra/logger` | `Logger logger` | Serial デバッグ出力 |
| `app/register_mode` | `RegisterMode regMode` | 登録ユースケースの制御フロー |
| `app/view` | `View view` | Serial + Display への出力 |
| `config.h` | — | コンパイル時定数 |
| `certs.h` | — | AWS IoT Core X.509 証明書・秘密鍵 |

### DDD 用語との対応

| DDD 用語 | 実体 |
|---|---|
| Value Object | `ThermometerData`, `Co2MeterData`（`SensorBase` 基底） |
| Aggregate + Repository | `Targets` |
| Application Service | `RegisterMode`, `.ino loop()` |
| Infrastructure Adapter | `BleScanner`, `Button`, `Lte`, `Logger` |
| Output Adapter | `View` |

## 依存ルール

依存方向は **上の層 → 下の層** のみ許可。

```text
.ino / app/register_mode
  └── domain/targets        (Targets)
  └── infra/*               (BleScanner, Button, Lte, Logger)
  └── app/view

app/view
  └── infra/ble_scan        (SensorVariant の型参照のみ)
  └── app/register_mode     (RegEntry の型参照のみ)

infra/ble_scan
  └── domain/sensor_factory (parse() 呼び出し)
  └── domain/targets        (targets.isTarget())
  └── app/register_mode     (regMode.isScanning/foundDevice)

infra/lte
  └── config.h + certs.h のみ

domain/* → config.h のみ
infra/*  → config.h + domain/* のみ（lte は certs.h も参照）
```

**禁止事項:**

- `domain/*` が `infra/*` や `app/view` を参照してはならない
- `infra/*` が `app/view` を参照してはならない

## グローバルインスタンス規則

各モジュールは `.cpp` にシングルトンインスタンスを定義し、`.h` で `extern` 宣言する。

```cpp
// domain/targets.cpp
Targets targets;

// domain/targets.h
extern Targets targets;
```

呼び出し元は `targets.load()`, `scanner.start()` のようなメソッド形式で使用し、
どのモジュールの機能かをコードで明示する。

## include 順序ルール

M5Unified は BLE ヘッダより**必ず先に** include しなければならない。
`view.h` が `<M5Unified.h>` を先頭で include しているため、`view.h` を最初に include することでこの順序を保証できる。

```cpp
#include "view.h"            // ← M5Unified をここで取り込む
#include "infra/ble_scan.h"  // ← BLE はその後
```

TinyGSM の define（`TINY_GSM_MODEM_SIM7080` 等）は `infra/lte.h` の include より**前に**書かなければならない。`infra/lte.h` 自体が先頭で define しているため、他ファイルで個別に define する必要はない。
