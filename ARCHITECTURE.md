# アーキテクチャ・命名規則

## システム全体構成

```
┌──────────────────────────────────────────────────────────────┐
│  デバイス層                                                    │
│  m5atom_iot_gateway/  (M5Atom S3 + ADS1115 + SIM7080G)       │
│    電圧測定・BLE スキャン → MQTT over TLS → AWS IoT Core      │
├──────────────────────────────────────────────────────────────┤
│  クラウド層 (infra/)                                           │
│  IoT Core → Topic Rule → Lambda(ingest) → S3                 │
│  API GW → Lambda(query/delete) → Athena → S3                 │
│  CloudFront → S3(web) → Web 管理画面                          │
└──────────────────────────────────────────────────────────────┘
```

## デバイス側レイヤー構成

```
┌─────────────────────────────────────────┐
│          Application 層                  │
│   m5atom_iot_gateway.ino               │
│   register_mode.h / .cpp                │
├──────────────────┬──────────────────────┤
│   Domain 層      │   Presentation 層    │
│   domain__*      │   view.h / view.cpp  │
├──────────────────┴──────────────────────┤
│         Infrastructure 層               │
│  infra__ble_scan, infra__button         │
│  infra__lte, infra__logger              │
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
infra__lte.h/.cpp               ← LTE/MQTT アダプター（SIM7080G + AWS IoT Core）
infra__logger.h/.cpp            ← Serial ロガーアダプター
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
| `infra__lte` | `Lte lte` | SIM7080G 制御・MQTT over TLS publish |
| `infra__logger` | `Logger logger` | Serial デバッグ出力 |
| `register_mode` | `RegisterMode regMode` | 登録ユースケースの制御フロー |
| `view` | `View view` | Serial + Display への出力 |
| `config.h` | — | コンパイル時定数 |
| `certs.h` | — | AWS IoT Core X.509 証明書・秘密鍵 |

### DDD 用語との対応

| DDD 用語 | 実体 |
|---|---|
| Value Object | `SwitchBotData` |
| Aggregate + Repository | `Targets` |
| Application Service | `RegisterMode`, `.ino loop()` |
| Infrastructure Adapter | `BleScanner`, `Button`, `Lte`, `Logger` |
| Output Adapter | `View` |

## 依存ルール

依存方向は **上の層 → 下の層** のみ許可。

```
.ino / register_mode
  └── domain__*   (Targets)
  └── infra__*    (BleScanner, Button, Lte, Logger)
  └── view

view
  └── infra__ble_scan  (SwitchBotData の型参照のみ)
  └── register_mode    (RegEntry の型参照のみ)

infra__ble_scan
  └── domain__switchbot_data  (parse() 呼び出し)
  └── domain__targets         (targets.isTarget())
  └── register_mode           (regMode.isScanning/foundDevice)

infra__lte
  └── config.h + certs.h のみ

domain__* → config.h のみ
infra__*  → config.h + domain__* のみ（lte は certs.h も参照）
```

**禁止事項:**
- `domain__*` が `infra__*` や `view` を参照してはならない
- `infra__*` が `view` を参照してはならない

## クラウドインフラ構成（infra/）

Terraform で管理。主要リソース：

| リソース | 役割 |
| --- | --- |
| AWS IoT Core Thing + 証明書 + Policy | デバイス認証・MQTT エンドポイント |
| IoT Topic Rule (`sensors/+/data`) | MQTT メッセージを Lambda ingest に転送 |
| Lambda `ingest` | JSON を S3 に保存（パーティション付き） |
| S3 バケット（データ用） | `raw/year=/month=/day=/hour=/` 階層構造 |
| Glue Database + Table | S3 データのスキーマ定義・パーティションプロジェクション |
| Athena Workgroup | SQL クエリ実行 |
| Lambda `query` | Athena 非同期クエリ発行・結果取得 |
| Lambda `delete` | Athena で対象特定 → S3 オブジェクト削除 |
| Lambda `authorizer` | API Key ヘッダ（`x-api-key`）検証 |
| API Gateway HTTP API | `GET /data`, `DELETE /data` |
| S3 バケット（Web 用） + CloudFront | 管理画面ホスティング |

## Web 管理画面（web/index.html）

- **単一ファイル** の静的 SPA（CloudFront + S3 で配信）
- Chart.js 4 で電圧・温度・湿度を独立したグラフで表示（アコーディオン切り替え）
- グラフ間でホバー縦線を同期（カスタム Chart.js プラグイン + AbortController）
- API エンドポイント・API Key・ラベル設定は `localStorage` に保存

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

TinyGSM の define（`TINY_GSM_MODEM_SIM7080` 等）は `infra__lte.h` の include より
**前に** 書かなければならない。`infra__lte.h` 自体が先頭で define しているため、
他ファイルで個別に define する必要はない。
