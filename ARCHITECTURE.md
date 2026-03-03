# アーキテクチャ・命名規則

## レイヤー構成

```
┌─────────────────────────────────────────┐
│          Application 層                  │
│   m5stamp_lite_test01.ino               │
│   register_mode.h / .cpp                │
├──────────────────┬──────────────────────┤
│   Domain 層      │   Presentation 層    │
│   domain__*      │   view.h / view.cpp  │
├──────────────────┴──────────────────────┤
│         Infrastructure 層               │
│         infra__*                        │
├─────────────────────────────────────────┤
│              設定                        │
│           config.h                       │
└─────────────────────────────────────────┘
```

## ファイル命名規則

| prefix | 層 | 説明 |
|---|---|---|
| `domain__` | Domain 層 | ビジネスロジック・Value Object・Repository |
| `infra__` | Infrastructure 層 | ハードウェア・外部システムのアダプター |
| なし | Application / Presentation | .ino・Application Service・View |

### 例

```
domain__switchbot_data.h/.cpp   ← Value Object（センサーデータ）
domain__targets.h/.cpp          ← Aggregate + Repository（監視対象リスト）
infra__ble_scan.h/.cpp          ← BLE ハードウェアアダプター
infra__button.h/.cpp            ← GPIO ハードウェアアダプター
```

### 二重アンダースコア（`__`）の意図

- 単一の `_` はスネークケースや略語（`btn_`, `_scan` 等）と区別がつかない
- `__` はレイヤー prefix であることを視覚的に明示する

## コンポーネント設計ルール

### 各ファイルの責務

| ファイル | クラス / インスタンス | 責務 |
|---|---|---|
| `domain__switchbot_data` | `SwitchBotData` (struct) | BLE 生データのパース・Value Object |
| `domain__targets` | `Targets targets` | 監視対象アドレスの管理・NVS 永続化 |
| `infra__ble_scan` | `BleScanner scanner` | BLE スキャン・データキューへの投入 |
| `infra__button` | `Button button` | GPIO ISR・ボタンイベント管理 |
| `register_mode` | `RegisterMode regMode` | 登録ユースケースの制御フロー |
| `view` | `View view` | Serial + Display への出力 |
| `config.h` | — | コンパイル時定数 |

### DDD 用語との対応

| DDD 用語 | 実体 |
|---|---|
| Value Object | `SwitchBotData` |
| Aggregate + Repository | `Targets` |
| Application Service | `RegisterMode`, `.ino loop()` |
| Infrastructure Adapter | `BleScanner`, `Button` |
| Output Adapter | `View` |

## 依存ルール

依存方向は **上の層 → 下の層** のみ許可。

```
.ino / register_mode
  └── domain__*   (Targets)
  └── infra__*    (BleScanner, Button)
  └── view

view
  └── infra__ble_scan  (SwitchBotData の型参照のみ)
  └── register_mode    (RegEntry の型参照のみ)

infra__ble_scan
  └── domain__switchbot_data  (parse() 呼び出し)
  └── domain__targets         (targets.isTarget())
  └── register_mode           (regMode.isScanning/foundDevice)

domain__* → config.h のみ
infra__*  → config.h + domain__* のみ
```

**禁止事項:**
- `domain__*` が `infra__*` や `view` を参照してはならない
- `infra__*` が `view` を参照してはならない

## Arduino ビルドシステムの制約

Arduino IDE / arduino-cli はスケッチルートの `.cpp` のみコンパイルする。
**サブディレクトリの `.cpp` はコンパイルされない。**

このため、すべての `.cpp` はスケッチルートに配置し、
レイヤー区別はファイル名 prefix で表現する。

## グローバルインスタンス規則

各モジュールは `.cpp` にシングルトンインスタンスを定義し、
`.h` で `extern` 宣言する。

```cpp
// domain__targets.cpp
Targets targets;

// domain__targets.h
extern Targets targets;
```

呼び出し元は `targets.load()`, `scanner.start()` のようなメソッド形式で使用し、
どのモジュールの機能かをコードで明示する。

## include 順序ルール

M5Unified は BLE ヘッダより **必ず先に** include しなければならない。
`view.h` が `<M5Unified.h>` を先頭で include しているため、
`view.h` を最初に include することでこの順序を保証できる。

```cpp
#include "view.h"          // ← M5Unified をここで取り込む
#include "infra__ble_scan.h"  // ← BLE はその後
```
