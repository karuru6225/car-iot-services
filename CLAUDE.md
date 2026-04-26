# car-iot-services プロジェクト設定

車載 IoT システム。M5Atom S3 + SIM7080G で車載バッテリー電圧・温湿度・CO2 を AWS IoT Core に送信し、Web 管理画面でグラフ表示する。

## よく使うコマンド

```bash
# Python（システム未インストール。PlatformIO 付属を使う）
~/.platformio/penv/Scripts/python.exe script.py

# ビルド（m5atom_iot_gateway ディレクトリで実行）
pio run

# ビルド＋書き込み
pio run -t upload

# シリアルモニタ
pio device monitor

# ビルド＋書き込み＋シリアルモニタ
pio run -t upload && pio device monitor
```

## コーディング規約（Arduino C++）

- インデント: スペース 2 つ
- 変数名: `camelCase`
- 定数: `UPPER_SNAKE_CASE`
- `delay()` よりノンブロッキング処理（`millis()` 利用）を優先する
- ピン番号は `const int` で定数化する

## KiCad ファイル編集禁止

`.kicad_sch` / `.kicad_pcb` / `.kicad_sym` / `.kicad_mod` などの KiCad ファイルを Claude が直接編集することは**絶対禁止**。

回路図・PCB への変更依頼が来た場合は、KiCad 上での操作手順を説明する形で回答すること。

## 開発フロー上の制約

このエディタ（Claude Code）でコードを編集し、PlatformIO でビルド・書き込みを行う。

- **コンパイル確認不可**: 構文エラーや型の不一致を見逃しやすいため、コードの正確性に特に注意する
- **ライブラリの存在を仮定しない**: 新たなライブラリを追加する際は明示的にユーザーへ伝え、PlatformIO 側でのインストールを促す
- **ボード設定は別ツールで管理**: ボード依存の設定をコード内で仮定する場合は必ずコメントで明記する
- **シリアルモニタは別ツール**: `Serial.print()` によるデバッグコードを追加する際はその旨をコメントで残し、不要になったら削除するよう提案する
- **書き込み後の動作検証は不可**: 不確かな変更はその旨を明示してユーザーに判断を委ねる

## プロジェクト参照

詳細な設計・構成は以下を参照：
- `CONTEXT.md` — システム概要・ハードウェア・データフロー・設計決定
- `ARCHITECTURE.md` — レイヤー構成・依存ルール・命名規則
