# テスト

## 対象範囲

`domain/` 層（ハードウェア・ネットワークに依存しない純粋ロジック、詳細は [ARCHITECTURE.md](ARCHITECTURE.md)）のみを、PlatformIOの `native` platform でホストPC上でユニットテストする。

`device/` / `service/` 層は Arduino/ESP-IDF API（`Wire`, `SerialAT`, NimBLE等）に直接依存しており、モック化しないとホスト上では動かせないためテスト対象外。実機動作の確認は `/pio` スキルでのビルド・書き込みに頼る。

## 実行方法

Windows/WSL いずれにも `native` platform に必要な `gcc`/`g++` が入っていない前提で、Dockerコンテナ内で実行する。

```bash
cd esp32_iot_gateway
docker compose -f docker-compose.test.yml run --rm test
```

初回は `build-essential` のインストールとPlatformIOの依存取得が走るため数分かかる。2回目以降はDockerのレイヤーキャッシュが効く。

`test/` 配下の各 `test_domain_*/` ディレクトリが個別のテストスイートとしてビルド・実行される（`pio test` が自動収集する）。

## 新しいdomainファイルのテストを追加する

1. `test/test_domain_<name>/test_<name>.cpp` を作成し、Unityで書く（既存ファイルを参考に）
2. `platformio.ini` の `[env:native]` の `build_src_filter` に対象の `.cpp` を追加する

```ini
build_src_filter =
    -<*>
    +<domain/obd.cpp>
    +<domain/foo.cpp>   ; ← 追加
```

**注意**: `test_build_src = true` により、`build_src_filter` で指定した全ファイルが `[env:native]` 内の**全テストスイートに共通で**ビルド・リンクされる。そのため、追加したファイルが未解決のシンボル（実機依存のグローバル関数等）に依存していると、そのファイルを直接使わない他のテストスイートまでリンクエラーになる。

## 実機依存の解決（スタブライブラリ）

`domain/telemetry.cpp` は以下の実機依存を持つが、`test/lib/` 配下にnativeテスト専用のスタブライブラリを置いて解決している（`test/lib/` はPlatformIOが自動でライブラリ探索する場所ではないため、`[env:native]` の `lib_extra_dirs = test/lib` で明示的に指定している）。

| 依存 | 実機側 | スタブ | 備考 |
|---|---|---|---|
| `<esp_rom_crc.h>` | ESP-IDF ROM関数 | `test/lib/rom_crc_stub/` | 標準CRC-32のビット単位実装。実機ROMとのビット一致は未検証。この関数を使う`MsgPackTelemetryEncoder`はテスト対象外にして影響を局所化している |
| `config.h` の設定値ゲッター（`getAhOffset()`等） | NVS実装（`config.cpp`、ESP-IDF依存） | `test/lib/config_stub/` | 固定値+テストから差し替え可能なsetterで代替 |

`<esp_rom_crc.h>` は山括弧includeなのでLDF（ライブラリ依存検出）がヘッダ名から自動的に `test/lib/rom_crc_stub` を見つけるが、`config.h` は相対パスのクォートincludeで既に解決済みのためLDFの自動検出が効かない。そのため `config_stub` は `[env:native]` の `lib_deps` に明示的に加えて強制リンクしている。

同様の実機依存を持つファイルを新たにテスト対象へ加える場合は、この2つと同じパターン（スタブライブラリ化 + 必要なら`lib_deps`へ明示追加）で解決する。

## 対象外にしているdomainファイル

- `sensor_filter.cpp` — `BLE_MEDIAN_FILTER` ビルドフラグ内で完結し `esp_sleep.h`（ESP-IDF）に依存。RTCメモリ経由のスリープ復帰判定を含み、モック投資が大きいため未対応
- `ble_targets.cpp` — `Preferences.h`（ESP32 NVSラッパー）に直接依存。config_stubと同様のスタブ化は可能だが未着手

## ASan / UBSan

`[env:native]` は `-fsanitize=address -fsanitize=undefined` を有効化している。バッファオーバーフロー等はメモリレイアウト次第でASanのスタック赤帯検知に掛からないことがある（`test_domain_thermometer` の off-by-one バグがまさにこのケースだった。構造体末尾のパディング領域に着地したためクラッシュせず、値のズレとしてassertが検知した）。ASanが沈黙していても値のassertは別途書くこと。
