# car-iot-services プロジェクト設定

車載 IoT システム。ESP32-S3 + SIM7080G で車載バッテリー電圧・電流・電力を AWS IoT Core に送信し、Web 管理画面でグラフ表示する。
アクティブな開発は `esp32_iot_gateway`。`m5atom_iot_gateway` は段階的廃止予定。

## よく使うコマンド

PlatformIO 操作は `/pio` スキルを使う（ビルド・書き込み・シリアルモニタ）。

```bash
# Python（システム未インストール。PlatformIO 付属を使う）
~/.platformio/penv/Scripts/python.exe script.py

# pio コマンド（PATH 未登録のためフルパスで実行）
~/.platformio/penv/Scripts/pio.exe run                        # ビルド
~/.platformio/penv/Scripts/pio.exe run -t upload              # ビルド＋書き込み
~/.platformio/penv/Scripts/pio.exe device monitor             # シリアルモニタ
~/.platformio/penv/Scripts/pio.exe run -e provision           # provision env でビルド
```

上記コマンドは `esp32_iot_gateway/` ディレクトリで実行する。

## esp32_iot_gateway ソース構造

3層アーキテクチャ（詳細は `esp32_iot_gateway/ARCHITECTURE.md`）:

```text
src/
├── main.cpp / config.h / config.cpp   # エントリポイント・全層共通定数・NVS
├── device/   # ハードウェアドライバ（lte, ads, ina228, oled, speaker）
├── domain/   # ビジネスロジック（measurement.h, telemetry）
└── service/  # ユースケース（mqtt, ota, logger）
```

include パスは `src/` 基準で書く:

- main.cpp から: `"device/lte.h"` / `"service/mqtt.h"` / `"domain/measurement.h"`
- service/ から: `"../device/lte.h"` / `"../config.h"`
- device/ 内 .cpp から: `"lte.h"`（同ディレクトリ相対）

## コーディング規約（Arduino C++）

- インデント: スペース 2 つ
- 変数名: `camelCase`
- 定数: `UPPER_SNAKE_CASE`
- `delay()` よりノンブロッキング処理（`millis()` 利用）を優先する
- ピン番号は定数化する（デバイス層: `static const uint8_t`、アプリ層: `#define`）

## KiCad ファイル編集禁止

`.kicad_sch` / `.kicad_pcb` / `.kicad_sym` / `.kicad_mod` などの KiCad ファイルを Claude が直接編集することは**絶対禁止**。

回路図・PCB への変更依頼が来た場合は、KiCad 上での操作手順を説明する形で回答すること。

## 開発フロー上の制約

このエディタ（Claude Code）でコードを編集し、PlatformIO でビルド・書き込みを行う。

- **コンパイル確認不可**: 構文エラーや型の不一致を見逃しやすいため、コードの正確性に特に注意する
- **ビルド確認**: ファイル移動・include 変更・構造変更の後は `/pio` でビルドが通ることを確認してからコミットする
- **ライブラリの存在を仮定しない**: 新たなライブラリを追加する際は明示的にユーザーへ伝え、PlatformIO 側でのインストールを促す
- **シリアルモニタは別ツール**: `Serial.print()` によるデバッグコードを追加する際はその旨をコメントで残し、不要になったら削除するよう提案する
- **書き込み後の動作検証は不可**: 不確かな変更はその旨を明示してユーザーに判断を委ねる

## プロジェクト参照

詳細な設計・構成は以下を参照：
- `CONTEXT.md` — システム概要・ハードウェア・データフロー・設計決定
- `esp32_iot_gateway/ARCHITECTURE.md` — レイヤー構成・依存ルール・命名規則
- `esp32_iot_gateway/CONTEXT.md` — esp32_iot_gateway 固有の詳細
- `esp32_iot_gateway/OTA.md` — OTA 仕様
