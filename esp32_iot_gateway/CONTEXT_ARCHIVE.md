# 完了TODOアーカイブ

`CONTEXT.md` の「作業中・引き継ぎ事項」セクションから、実装済み/対応済みになったTODO、および対応しない方針に決めたTODOをここに移した。現在進行中のTODOは `CONTEXT.md` を参照。

### ~~TODO: trip_analysisの通信断ギャップで燃費が破綻するケースがある~~ **修正済み**

2026-08-22のトリップで距離2.03km・燃料消費0.005L・燃費372km/Lという物理的にありえない値が見つかった件（`_compute_summary()`が通信断に隣接するギャップの燃料をゼロ扱いにする一方、距離はGPS座標の差分でそのまま積算していたため）を修正した。

- `_compute_summary()`: `prev`/`cur`のどちらかが`_is_comm_dropout()`ならその区間の距離・燃料を両方とも積算から除外する方針（案1、最も保守的）を採用
- 保険として`_fuel_economy_car_comparison()`に`|z|>=3.0`を「大きく外れた値＝計測・集計の異常を疑うべき」という区分として追加。根本原因の修正だけに頼らず、AIナレーティブに「燃費は良好」のような誤った評価を渡さないようにした
- AIナレーティブ機能を実運用データで試した際に発覚した（AIが372km/Lをそのまま「良好な結果」と報告してしまうことでユーザーが気づいた）

### ~~TODO: OBDトリップのAIナレーティブ生成~~ **実装済み**

`experiments/trip-analysis-ai-poc/`（gitignore対象）でのプロンプト設計検証（DESIGN_LOG.md）に基づき、`infra/lambda_src/trip_analysis/index.py`にBedrock連携を実装した。

- **自動実行ではなく、Web管理画面から特定のトリップを指定して手動実行する方式**
  （`POST /trip-analysis/narrative {device_id, key}` → `_handle_regenerate_narrative()` →
  `_regenerate_trip_narrative()`）。当初は`_process_job()`に直接組み込んでいたが、
  「新規トリップにしか生成されない・過去のトリップに遡って生成できない」という制約が
  あったため、個別指定・何度でも再実行可能な方式に変更した。`_load_trips()`が返す
  各トリップに`analysis_key`（S3キー）を付与し、Web側が対象を特定できるようにしている
- `_bucket_rows()`/`_bucket_csv()`: 30秒バケットの時系列（`FAST_FIELDS`のmax/min/mean、`SLOW_FIELDS`のmean）
- `_compute_trip_baseline()`/`_compute_row_baseline()`: 過去トリップの`row_count`重みつき統計。行単位のz-score母集団は、POCのように過去の生データを毎回読み直すのではなく、`_compute_summary()`に追加保存した各トリップのavg/std/row_countをpooled variance公式で合成して復元する設計に変更（Athenaへの追加クエリを避けるため）
- `_label_for_zscore()`/`FIELD_LABELS`: z-scoreを自然言語ラベル（「やや薄め」等）に変換
- `_reverse_geocode()`/`_describe_location()`: AWS Location Service（Here、Terraformでリソース化）で逆ジオコーディング。自宅は`HOME_LAT`/`HOME_LON`（`terraform.tfvars`のsensitive変数）+半径判定で匿名化
- `_is_comm_dropout()`: 通信断（coolant_c/ecu_voltage同時ゼロ）の集計除外
- `_build_narrative_prompt()`/`_invoke_bedrock()`: POCで確定した最終プロンプト構成（①サマリー②数値指摘③平易な説明④提案）でBedrock Nova Pro（`apac.amazon.nova-pro-v1:0`、converse API）を呼び出す
- Lambdaランタイム同梱のbotocoreがconverse APIに対応していない場合があるため、固定バージョンのboto3/botocoreをLambda Layerとしてバンドル（`infra/lambda_src/trip_analysis/layer/`、pip installは手動一回限りの手順）
- `_generate_narrative()`は失敗時に空文字へフォールバックする（`_regenerate_trip_narrative()`はそれをそのまま`narrative`へ書き込むため、生成失敗時は空文字で上書きされる）
- Web管理画面（`web/trip-analysis.html`）のトリップ一覧はテーブルからアコーディオンに変更し、展開すると詳細統計とナレーティブ・「AI分析を実行」ボタンが表示される
- 残課題として認識した上で対応しなかった点: 一般論的な注意喚起（「点検がおすすめです」等）を完全にゼロにする指示は、POC検証で複数回試しても効果が不完全だった。「④提案」への隔離で実害は抑えられているため、追加対応はせず許容する方針とした

### ~~TODO: Shadow データを S3/Athena に流す（時系列履歴の保存）~~ **設計変更により対応済み**

Shadow はテレメトリではなく設定値（ah_offset / fw_version）を管理するよう刷新。
バッテリーテレメトリは `sensors/{device_id}/data`（type=`"battery"`）として送信し既存の ingest パイプラインで S3 に蓄積。
Shadow の desired / delta による双方向リモート設定変更も実装済み。

### ~~OTA ファームウェアの gzip 圧縮~~ **v1.13.0 で実装済み**

CIが`firmware.bin.gz`を生成しS3にアップロード（`deploy_ota.ps1`は使われなくなり削除済み）。
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
| 温度 | `f3a8b2c6-...` | float32 | °C（ina228.readTemp） |
| Ah | `f3a8b2c7-...` | float32 | 積算電荷量（ina228.readCharge() + getAhOffset()） |
| タイムスタンプ | `f3a8b2c8-...` | uint32 | UNIX時刻（秒） |
| LTE接続状態 | `f3a8b2c9-...` | uint8 | 0=未接続, 1=接続中（lte.isConnected） |

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
- スマホ接続 → CONTINUOUS モードに昇格（measure + publish は5分おき、BLE Notifyは1秒おきに独立して実行）
- スマホ切断 → DEEP_SLEEP に戻る（手動 CONTINUOUS の場合は維持）
- DeepSleep 前に `rtc_gpio_pullup_en(GPIO_NUM_0)` + `esp_sleep_enable_ext0_wakeup(GPIO_NUM_0, 0)` を登録済み。BOOT ボタン押下で DeepSleep から即時復帰できる

#### Phone メニュー（ペアリング）

- OLED メニュー「Phone」を選択 → 6桁の Passkey を OLED に表示
- スマホ側でコードを入力してペアリング → CONTINUOUS モードに移行
- ボンディング情報は NVS に保存（`MAX_BONDS=1`：再ペアリングで上書き）
- ペアリング後は次回から自動接続 + 設定 R/W 可能

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

### ~~TODO: data_bin パケット破損検知（CRC32 + マジックナンバー）~~ **v1.18.0 で実装完了**

ESP32↔SIM7080G間のUARTにパリティ・CRCが無く、稀な1ビット化けがそのままクラウドまで届く問題が発覚（Shadow reportedのキー名が化けて大量蓄積する形で顕在化）。`sensors/{device_id}/data_bin` に以下の対策を実装（詳細は「MQTT ペイロード形式」節のバイナリフレーミング参照）:

- ファーム側 (`domain/telemetry.cpp::MsgPackTelemetryEncoder::serialize`): `[magic 0xC1][version][MessagePack本体][CRC32 4B]` でラップ
- `ingest` Lambda (`infra/lambda_src/ingest/index.py`): magicバイトの有無で新旧フォーマットを自動判別（デプロイ順序に依存しない）、CRC不一致時は専用バケット `corrupted`（30日で自動削除、`infra/s3.tf`）に生バイナリを退避し `raw/` のAthenaスキーマは無傷に保つ
- S3保存JSONとログにフォーマットバージョン（`"ver"`: 0=旧形式, 1=新形式）を出力し、破損率の実測・監視を可能にした

Shadow（JSON テキスト）側は同種の対策は未実装だったが、下記の `shadow_guard` で別方式の対策を実装した。破損が疑われる場合は `aws iot-data delete-thing-shadow` で一度リセットすれば次サイクルで自動再構築される（`reported` はマージ方式で蓄積し続けるため）。

### ~~TODO: Shadow reported の不正キー検知・自動修正~~ **実装完了**

`$aws/things/{id}/shadow/update` は AWS IoT の予約トピックで Device Shadow サービスが直接マージするため、data_bin のように Topic Rule でマージ前に横取りして弾くことができない。そのため事後検知＋即時修正で対応する:

- Topic Rule `shadow_guard`（`infra/iot.tf`）: `$aws/things/+/shadow/update/accepted` を購読。このトピックの `state.reported` にはその回の更新で変化したキーのみが載るため、全体を再取得せずに差分だけを検証できる
- Lambda `shadow_guard`（`infra/lambda_src/shadow_guard/index.py`）: `telemetry.cpp::buildConfigPayload` が送るキーのホワイトリスト（キー名 + 型）と照合。未知キー・型不一致・`override_next_mode` の不正値を検知したら該当キーを `null` で上書きする `update-thing-shadow` を発行し reported から除去する（null は検証対象外にしているため、この修正自体が再度検知されて無限ループする心配はない）
- **既知の限界**: 許可キーのまま値だけ壊れるケース（例: `chg_start_v` の型は正しいが桁が飛ぶ）は検出できない。あくまでキー名の化けによる未知キー蓄積の防止が目的
- **保守上の注意**: `telemetry.cpp::buildConfigPayload` の reported フィールドを追加・変更したら `shadow_guard/index.py` の `SCHEMA` も合わせて更新すること

### ~~TODO: S3 raw データの定期 Compaction~~ **実装済み**

`raw/year=.../month=.../day=.../hour=.../{device_id}-{uuid8}.json`（`infra/s3.tf` / `infra/lambda_src/ingest/index.py`）は ingest Lambda がメッセージ1件（デバイスの publish 1回）ごとに1オブジェクトとして書き込む設計。稼働台数・稼働時間が伸びるほど数十〜百バイト級の小さいJSONファイルが hour パーティション配下に大量に溜まっていく。

**目的**: Grafana（`claude-opentelemetry/grafana/provisioning/dashboards/iot-monitor.json`）が Athena経由で `sensor_data` テーブルを常時クエリしており、そのコスト削減が主目的。ダッシュボードのデフォルト時間範囲は `now-24h` で、7パネルの多くが直近2日分（`CONCAT(year,month,day,hour) BETWEEN ...` によるパーティションプルーニング）を実質的なスキャン対象にしている。**そのため compaction は直近データから始めて構わない**（「古いデータだけ」に限定すると本来の目的であるクエリコスト削減効果が薄れる）。

**制約1（Grafanaクエリは無改修）**: 上記7パネルの設定・SQLは変更しない前提。パーティションプルーニングに `CONCAT(year, month, day, hour) BETWEEN ...` を使うため、パーティションカラム構成・テーブル名・カラム名は今のまま維持する必要がある。

**確認済み**: `aws glue get-table` でテーブル定義を確認したところ、`InputFormat=TextInputFormat` + `SerDe=org.openx.data.jsonserde.JsonSerDe`（改行区切りJSONを複数行読める）、かつ `projection.enabled=true`（Glue Crawler不要、S3プレフィックスから仮想的にパーティション解決）だった。この2点により、**テーブル定義・Grafanaクエリを一切変更せずに compaction できる**。

**制約2（行単位削除の劣化・許容）**: `DELETE /data`（`infra/lambda_src/delete/index.py`）は Athena の `$path`（＝`raw/`配下の個別オブジェクトキー）を使って行単位削除している。直近データもcompaction対象にするため、compaction 後は削除粒度が「行」から「マージ後ファイル全体（＝該当時間帯の全行）」に落ちるのは**現時点では許容する**。将来的に `delete/index.py` を「対象keyがマージ済みファイルなら GetObject → 該当行を除いて PutObject で上書き」という動作に改修すれば行単位削除を維持できる（`_delete_by_keys` の分岐追加で対応可能、ファイルサイズは小さいままなので読み直しコストは軽微）。この改修は本 TODO のスコープ外とし、別 TODO として切り出す。

**compaction の単位**: 1 hour パーティション（`raw/year=.../month=.../day=.../hour=.../`）＝1マージファイル。年月日時の値はS3パス（projection）から機械的に決まり行内容の `ts` は見ないため、**hour境界をまたいでマージすると行の実時刻とパーティション値がズレて `CONCAT(year,month,day,hour) BETWEEN ...` のプルーニングが壊れる**。device_id はパーティションカラムではないので、同一hour内なら複数デバイスを1ファイルに混ぜてよい（現状1時間あたり平均約29件なので1ファイルで十分小さい）。

**タイムゾーンの注意**: `year/month/day/hour` はすべてUTCで書き込まれている（`ingest/index.py` の `datetime.now(timezone.utc)` / `ts` パース時も `tzinfo=timezone.utc` を付与）。Grafanaダッシュボードは表示上 `"timezone": "browser"`（＝JST）だが、`$__timeFrom()`/`$__timeTo()` マクロが解決するのは絶対時刻（UTC epoch）なので `date_format(...,'%Y%m%d%H')` は問題なくUTCパーティション値と一致する（表示がJSTなだけでクエリのズレはない）。compaction 実装側で気をつけるべきは:

- 「対象は確定済み＝現在進行中でない past hour」の判定は**UTCで**行う（Lambdaの `datetime.now(timezone.utc)` を使う。ローカル実行時のマシン時刻(JST)や `datetime.now()`（naive）をそのまま使わない）
- EventBridge Scheduler の cron/rate 式はデフォルトUTC評価。スケジュール定義時に誤ってタイムゾーンをAsia/Tokyo等に設定しない（設定した場合は「past hour」判定側もそれに合わせてズレるため、UTC固定に統一するのが安全）

**実装内容**: `infra/lambda_src/compact/index.py`（Lambda名 `iot-monitor-compact`）が対象 hour パーティション配下の小さいJSONファイル群を読み、NDJSONとして1ファイル（`merged.json`）にまとめて同じプレフィックスへ再書き込みする。元の小ファイルはアーカイブへ退避後、S3 Lifecycle expirationで自動削除。EventBridge Scheduler で1時間に1回起動（UTC評価）。`s3-compaction-infra`ブランチをPR #3として2026-08-01にmainへマージ、`terraform apply`済みで稼働中。

過去データ約105,000件のバックフィルのために一時的にパーティション単位並列化・バッチ処理化まで複雑化したが、バックフィル完走後に単純化（通常運用は1〜2パーティション/時なので逐次処理で数秒で終わる）。`lambda.tf`の`reserved_concurrent_executions=1`は同時実行による行重複防止のため意図的に残置。

**効果**: `raw/`配下の総オブジェクト数が104,688（compaction前）→3,126（compaction後）と約97%減少（2026-08-01確認）。

### ~~TODO: NimBLE ペリフェラル・ブロードキャスター無効化~~ **実装済み**

`platformio.ini` の release env に `CONFIG_BT_NIMBLE_ROLE_PERIPHERAL_DISABLED=1` / `CONFIG_BT_NIMBLE_ROLE_BROADCASTER_DISABLED=1` を追加。Flash **約16KB 削減**（696,693 → 680,165 bytes）。

「BLE ダッシュボード表示器」「スマホ BLE 連携」TODO を実装する際は Peripheral が必要になるため、その時点で削除する。

### ~~TODO: OBD-II（CAN）実車データ取得~~ **実装済み**

`device/can.h/.cpp`（TWAI・29bit拡張アドレッシング・N-VAN対応）、`domain/obd.h/.cpp`（全28PIDデコード）、`service/obdpoll.h/.cpp`（1秒間隔逐次ポーリング）を新規実装。取得値はOLED表示（`oledShowObdData()`）とBLE Notify（`MEAS_OBD_UUID`、87バイトを18バイトずつ5チャンクに分割）でスマホアプリ（`mobile/lib/main.dart`）へ送信する。導入当初は専用モード`OperationMode::CONTINUOUS_OBD`として実装したが、後日の整理で通常の`CONTINUOUS`モードにOBDポーリングが統合され、専用モード・専用メニュー項目・専用Shadow override値は廃止された（現在は`CONTINUOUS`に入れば自動でOBDポーリングも動く）。

AWS への publish（`domain/telemetry`統合）は今回のスコープ外で未実装。詳細（実車スキャン結果・CANプロトコル詳細・ビットマスク等）は `OBD.md` 参照。

### ~~TODO: ISO-TPマルチフレーム対応・多PID要求~~ **実装済み（PR #6、v1.21.0）**

高レート(10Hz級)OBDポーリング「ダイノモード」構想の前段として、CAN層の制約（マルチフレーム受信未実装・多PID要求未対応）を解消した。`isotp-multipid`ブランチをPR #6として`main`へマージ、`v1.21.0`としてOTAリリース済み（v1本番車両、2026-08-07）。実車確認まで完了。

- `can.cpp`にISO-TP（ISO 15765-2）受信の最小実装を追加: SF/FF/CFをPCIバイトで分岐、FF受信時にFlow Controlを自動送信、Consecutive Frameを組み立てる（`canReceiveObdResponse()`に`maxLen`引数追加）
- `can.cpp`に多PID要求送信（`canSendObdRequestMulti()`）、`obd.cpp`に多PID応答パーサ（`obdParseMultiResponse()`、PID→データ長テーブル`kPidLengths[]`でTLV的に分解）を追加
- `obdpoll.cpp`の29PIDポーリングを`kMaxPidsPerRequest`=6ずつ・5リクエストにバッチ化（異常時最悪サイクル時間 29×50ms=1.45秒→5×50ms=250ms程度に短縮）
- PID 0x68（Charge Air Cooler Temp、マルチフレーム必須）の正式デコーダを実装し`iat_c`/`iat2_c`として`OBDReading`/`ObdBlePacket`/mobile側/Lambda ingest/Athenaスキーマへ反映
- 過去の「多PID要求で1個だけ返る」不具合の原因が受信側の実装不備（ECU側は多PID対応）と実車確認で確定。物理アドレッシングは不要と判断し見送り

**残課題**: センサー1/2とインタークーラー前後の物理対応は未確定（保留）。実走行（速度・RPM・負荷変動）およびIGN OFF/CAN未接続時の異常系パスは未検証のままリリース済み（アイドル・停車・IGN ONの範囲のみ確認）。単発PID版`canSendObdRequest(uint8_t pid)`は現在どこからも呼ばれず未使用のまま残存。

### ~~TODO: OTA失敗時にAWS IoT Jobsへ FAILED を報告しない~~ **実装済み**

`Ota::apply()`（[ota.cpp:154-232](esp32_iot_gateway/src/service/ota.cpp#L154-L232)）の全ての失敗パス（ダウンロード失敗・`esp_ota_begin`失敗・書き込み失敗・`esp_ota_end`失敗・boot partition設定失敗）が`false`を返すだけで`jobsReport(job.id, "FAILED", ...)`を呼ばない。`handleJob()`は既に`IN_PROGRESS`を送信済み（[ota.cpp:333](esp32_iot_gateway/src/service/ota.cpp#L333)）だが、失敗時に`FAILED`が送られないため、AWS側でJobが`IN_PROGRESS`のまま永久に残り続ける。呼び出し元の`main.cpp`も`ota.handleJob(job)`の戻り値を見ていない。

**実装方針（案）**: `apply()`の各失敗パスに`jobsReport(jobId, "FAILED", <理由>)`を追加する。`apply()`内で`jobId`を保持しているので、失敗理由（ダウンロード失敗/書き込み失敗/検証失敗等）を`statusDetails.reason`に含めれば運用側の切り分けにも使える。

### ~~TODO: Shadowのoverride_next_modeが起動直後しか反映されない~~ **対応しない方針**

`getShadowOverrideMode()`は`setup()`内で1回だけ呼ばれる（`main.cpp`）。一方`shadowPollDelta()`は`continuousLoopCore()`の1秒ティックや`enterDeepSleepMode()`、`loop()`本体でも呼ばれ、そこで`override_next_mode`のdeltaを受け取ると`s_overridePending`をセットしAWSへACK（`shadowPublishConfig(true)`）まで返してしまう（`shadow.cpp`）が、`s_overridePending`を実際にモードへ反映する`getShadowOverrideMode()`の呼び出しは起動時の1回しかないため、稼働中に送ったoverride_next_modeは「reportedは更新されたのに実際のモードは変わらない」という状態になる。

**判断**: 現状の挙動（起動直後のみ反映）のまま許容することにした。`override_next_mode`は元々「次回起動時に1サイクルだけ」という起動タイミング前提の機能（`ONE_SHOT_CONTINUOUS`）であり、稼働中に送っても即座に反映される必要性は薄いと判断。

### ~~TODO: lte.cpp fileReadChunk()がモデム応答サイズをバッファ容量でクランプしない~~ **実装済み**

`Lte::fileReadChunk()`（`device/lte.cpp`）はSIM7080Gの`+CFSRFILE`応答から`actual`（実際のサイズ）を読み取るが、呼び出し側バッファの容量`maxLen`と突き合わせずに、先行データのコピーループ・`readBytes()`双方でその`actual`バイト分をそのまま書き込んでいた。モデムが誤応答（`actual > maxLen`）を返した場合、呼び出し元の`writeGzToOta()`（`service/ota.cpp`）が使う固定4096バイトバッファ`s_gz.buf`を越えて書き込む可能性があった。

**対応**: `actual > maxLen`の場合はエラー（-1）として扱うようにした（超過分の破棄ではなくエラー扱い。誤応答は元データの信頼性も疑わしいため）。

### ~~TODO: https.cpp のダウンロードチャンク受信も同様にサイズ未検証~~ **実装済み**

`waitShreadHeader()`（`service/https.cpp`）も`+SHREAD:`応答から解析した`actual`を、固定2048バイトの`static chunk[]`バッファの容量（呼び出し側が要求した`readLen`）と突き合わせずに`SerialAT.readBytes()`へ渡していた。上記lte.cppの`fileReadChunk()`と同一パターンのバグ。

**対応**: `waitShreadHeader()`内で`actual > chunkSize`ならエラー（-1）を返すようにした。呼び出し元（`Https::get()`）は既存の`actual <= 0`エラーハンドリングをそのまま利用できる。

### ~~TODO: main.cppがセンサー/CAN初期化の失敗を無視している~~ **実装済み**

`setup()`内の`adsInit()`/`ina228.init()`/`canInit()`はいずれも初期化失敗を伝えるため`bool`を返す設計だが、戻り値を誰も確認していなかった。I2C接続不良等で初期化に失敗しても異常を検知する手段がどこにもなかった。

**対応**: 各初期化の戻り値を見て失敗時に`logger.println()`で警告ログを出すようにした（`logger.println()`はSPIFFSログにも永続化されるため事後調査可能）。telemetryペイロードへの「センサー異常」フラグ追加やOLED警告表示は見送り（現状のログ出力で検知は可能なため）。

### ~~TODO: jobs.cppがacceptedトピックを厳密一致で判定していない~~ **実装済み**

`jobsGetNext()`（`service/jobs.cpp`）は`rejected`トピック文字列を生成するが使わず、`strstr(recvTopic, "rejected")`が偽であれば無条件にacceptedレスポンスとして処理していた。`shadow.cpp`の`shadowPollDelta()`が`strcmp`で厳密一致を取っているのと対照的だった。

**対応**: `rejected`トピックとの一致を`strcmp`に変更し、さらに`accepted`トピックも生成して`strcmp`で厳密一致を取ってから処理するようにした。想定外のトピックはログを出して無視する。

### ~~TODO: ota.cpp のバージョン一致判定が前方一致になっている~~ **実装済み**

`Ota::handleJob()`の同一バージョンスキップ判定が`strncmp(FIRMWARE_VERSION, version, strlen(version)) == 0`で、`version`が`FIRMWARE_VERSION`の前方一致であれば真になっていた。`FIRMWARE_VERSION`は`"<base>+<githash>"`形式なので、短いversion文字列がたまたま前方一致すると誤って「同一バージョン」と判定されOTAがスキップされる可能性があった。

**対応**: `isSameFirmwareVersion()`ヘルパーを追加し、`FIRMWARE_VERSION`の`+`より前の部分（base部分）と`version`をサイズ込みで完全一致させるようにした。

### ~~TODO: gzLog()がログ永続化をスキップしている~~ **実装済み**

`gzLog()`（`service/ota.cpp`）は`Logger::printf`と同じ`vsnprintf`パターンを再実装していたが、最後に`logger.print(buf)`を呼んでおり、`logStorageWrite()`を呼ばずSerial出力のみで終わっていた。OTAのgz解凍時に出る`[OTA] gz...`系のログ行だけがSPIFFS上のデバッグログファイルに残らなかった。

**対応**: `logger.print(buf)`を`logger.printf("%s", buf)`に差し替え、`logStorageWrite()`経由でも永続化されるようにした。

### ~~TODO: device/がservice/logger.hを参照している（レイヤー依存違反）~~ **実装済み**

`device/lte.cpp`・`device/can.cpp`・`device/ble_peripheral.cpp`が`#include "../service/logger.h"`しており、ARCHITECTURE.mdの「device/は上位層（domain/service）をincludeしてはいけない」というNG例と同じパターンが3ファイルで発生していた。

**対応**: `logger.h`/`logger.cpp`を`service/`から`src/`直下（`config.h`/`board_pins.h`と同格）へ移動。ARCHITECTURE.mdも元々`logger.h/.cpp`を「横断的関心事」と記述していた（物理配置だけがそれに追いついていなかった）ため、ドキュメントの意図に実装を合わせる形になった。`logger.cpp`自体は引き続き`service/log_storage.h`（ログのSPIFFS永続化）に依存するが、これは横断的関心事の実装詳細であり、レイヤー依存ルール（device/domainが上位層をincludeしてはいけない）の対象外として扱う。全14箇所のinclude（device/3、service/10、main.cpp）を更新。ARCHITECTURE.md・CLAUDE.mdのファイル構成図も合わせて更新。

### ~~TODO: main.cppのGPIO_NUM_0がピン定数化されていない~~ **実装済み**

`enterDeepSleepMode()`内で`GPIO_NUM_0`が4箇所生の値のまま使われており、CLAUDE.mdの「ピン番号は定数化する」規約に反していた。BOOTボタン（wake用）のピンであることがコードから読み取りにくかった。

**対応**: `main.cpp`内に`#define WAKE_PIN GPIO_NUM_0`を追加し、4箇所を置き換えた。ESP32-S3モジュール内蔵のBOOTボタンで基板（v1/v2）によらず固定のため、`board_pins.h`（基板ごとに異なるピンを管理する場所）ではなく`main.cpp`側（アプリ層）での`#define`とした（CLAUDE.mdの「ピン番号は定数化する（アプリ層: `#define`）」規約に対応）。

### ~~TODO: 充電ヒステリシスロジックがmain.cppに直書きされている~~ **実装済み**

`updateChargingState()`は電圧閾値によるヒステリシス判定という、ハードウェア・ネットワーク非依存の純粋ロジックだが、`domain/`ではなくエントリポイントの`main.cpp`に直接書かれていた。

**対応**: `domain/charging.h`/`.cpp`に`decideCharging(vMain, vSub, currentlyCharging, ChargingThresholds) -> bool`という純粋関数を切り出した。`main.cpp`側の`updateChargingState()`は判定結果を受けて状態が変わった場合のみ`setCharging()`/`digitalWrite()`/ログ出力するだけになった。`test/test_domain_charging/`にnative環境のユニットテスト8件を追加（起動条件・停止条件・電圧異常時の非遷移などを網羅）。

### ~~TODO: service/https.cppのparseUrl()がハードウェア非依存なのにdomain/にない~~ **実装済み**

`parseUrl()`はURL文字列をhost/pathに分解するだけの純粋関数で、ハードウェア・ネットワークに一切依存していなかったが、`service/https.cpp`内に`static`関数として直書きされていた。

**対応**: `domain/url.h`/`.cpp`に切り出した。`service/https.cpp`は`#include "../domain/url.h"`して呼ぶだけになった。`test/test_domain_url/`にnative環境のユニットテスト5件を追加（https/httpスキーム判定、スラッシュなし時のpath="/"デフォルト、未知スキーム拒否、hostバッファ溢れ時の拒否）。

### ~~TODO: service/obdpoll.cppのboost/燃費派生値計算がdomain/にない~~ **実装済み**

`finalizeAndLog()`内の`boost_kpa`（MAP-Baro、Baro未取得時は標準大気圧101kPaで代用）・`fuel_rate_lph`（MAFからの燃費推算）の計算は`OBDReading`のフィールドだけで完結する純粋ロジックだったが、ログ出力と同じ関数に同居しており`domain/obd.cpp`の他の`obdDecode*()`群とは別の場所にあった。

**対応**: `domain/obd.h`/`.cpp`に`obdComputeDerived(OBDReading &r)`を追加し、計算部分を移した（`r.valid`が`false`の場合は何もしない）。`obdpoll.cpp`の`finalizeAndLog()`は`obdComputeDerived(r)`を呼んだ後にログ出力するだけになった。`test/test_domain_obd/`にユニットテスト5件を追加（Baro代用、fuel_rate計算、MAF未取得時の据え置き、valid=false時のスキップ）。

### ~~TODO: speaker.cpp playMelody()がdelay()でブロッキングしている~~ **実装済み**

`playMelody()`は`tone()`の後に`delay(n.dur)`で音符ごとにブロッキングしており、CLAUDE.mdの「`delay()`より`millis()`利用のノンブロッキング処理を優先」に反していた。

**対応**: `delay(n.dur)`を、`millis()`で経過時間を見ながら`delay(1)`刻みでポーリングする待機ループに置き換えた（`continuousLoopCore()`の待機ループと同じパターン）。音符ごとの実際の待機時間・呼び出し側の挙動は変わらないが、タイミング管理を固定長`delay()`から`millis()`比較へ移した。呼び出し箇所は`main.cpp`の起動時2箇所（`bootStart`/`boot`）のみで非同期再生の需要が無いため、`speakerTick()`のような外部公開の状態遷移関数は追加していない（`playMelody()`内部に閉じたポーリングループで完結）。

### ~~TODO: menu.cpp tickCharging()にdelay()が残っている~~ **実装済み**

`tickCharging()`自体は`millis()`駆動の状態機械だが、遷移点3箇所（充電不可・完了・停止）で`delay(1000〜2000)`が挟まっており、その間`enterMenuMode()`の待機ループが呼ぶ`button.read()`が実行されずボタン入力を取りこぼしていた。

**対応**: メッセージ表示専用の`MenuState::MESSAGE`を追加し、`beginMessage(title, body, durationMs, next)`でOLED表示・経過時間計測を開始、`tickMessage()`で`millis()`経過を見て`next`状態へ遷移させるようにした。`tickCharging()`側の3箇所は`delay()`呼び出しをこの`beginMessage()`への置き換えのみで対応でき、メッセージ表示中も`enterMenuMode()`の50msポーリングループ（`button.read()`含む）が回り続けるようになった。

同じ「`oledShowMessage()`→`delay()`→次状態へ遷移」の形をしていた他4箇所（`tickBleScanResult()`のBLE登録完了、`tickBleRemoveConfirm()`の削除完了、`tickAhOffset()`/`tickChgTimeout()`の保存完了）も`beginMessage()`に置き換えた。`ConfirmDef.onConfirm`を`void(*)()`から`MenuState(*)()`に変更し、`doAhReset()`も`beginMessage()`を返す形にした。`doNvsClear()`のみ直後に`esp_restart()`で戻らないため`delay(1500)`のまま残した（再起動直前でボタン監視が止まっても実害がないため対象外とした）。`beginMessage()`/`tickMessage()`とその状態変数は全`tick*()`関数から参照できるよう`MenuState`列挙体の直後（`ConfirmDef`より前）に定義位置を移した。

### ~~TODO: CONTINUOUSモード中、5分境界ごとにOBDポーリングが10秒以上途切れる問題~~ **実装済み・実機確認済み**

実車走行データ（Athena `obd_data`、2026-08-08）分析で、CONTINUOUSモードの5分境界直後だけOBDポーリング間隔が14〜19秒に伸びる欠損が判明。原因は`service/monitor.cpp`の`measure()`が呼ぶ`bleScanner.start(SCAN_TIME)`（`SCAN_TIME=10`）が`NimBLEScan::start(uint32_t, bool)`（ブロッキング同期版、NimBLE-Arduino v1.4.0のソースで確認）を使っており、スキャン中`continuousLoopCore()`の1秒ティックOBDポーリングが完全に止まっていたこと。

**対応**: `device/ble_scan.h/.cpp`に非同期版`startAsync()`・`isScanning()`・`stop()`を追加（既存の`start()`はブロッキングのまま残し、`service/menu.cpp`のBLE登録画面はそちらを使い続ける）。`service/monitor.cpp`の`publish()`を`publishBattery()`/`publishBle()`に分割し、`collectBle()`（スキャン完了時のみキュー収集、未完了ならno-op）を追加。`main.cpp`に`g_blePending`フラグと`pollBleCollect()`を追加し、`continuousLoopCore()`の既存1秒ティックループ内でBLEスキャン完了を非ブロッキングに拾うようにした。`enterDeepSleepMode()`には`queue.save()`直前に有界待機（`SCAN_TIME+3`秒でタイムアウトし`bleScanner.stop()`で強制打ち切り）を追加し、DEEP_SLEEPモードが「スリープ前にそのサイクルのBLE結果をqueueに保存する」という既存の保証を崩さないようにした。

**制約**: この修正はdevice/service/main.cppにまたがり、native Unityテストの対象外（ハードウェア/FreeRTOS依存のため）。`pio run`のコンパイル確認と既存native domainテスト49件のパスに加え、実機検証も実施済み。

**実機検証結果（2026-08-09）**: 5分境界を2回（08:20:00・08:25:00 UTC）またぐ実車走行データをAthena `obd_data`で確認。`car-iot-aca704`の08:19:03〜08:25:35（182サンプル）で`obd_ts`間隔5秒以上のギャップは0件（修正前は境界のたびに14〜19秒の欠損が確実に発生していた）。両境界とも間隔は通常通り2〜4秒で継続しており、修正の効果を確認した。DEEP_SLEEPモード側のBLEデータ保存継続は未検証（走行中はCONTINUOUSモードが主のため、別途確認が必要）。

### ~~TODO: domain/obd.hのフィールド名がsnake_case（CLAUDE.md命名規約違反）~~ **実装済み**

`OBDReading`・`ObdBlePacket`の約55フィールド（`speed_kmh`、`map_kpa`、`sec_o2_trim_lt_pct`等）が全て`snake_case`で、ルート`CLAUDE.md`の「変数名: camelCase」規約に反していた。

**対応**: `test/test_domain_obd/test_obd.cpp`を先にcamelCase名（`speedKmh`等）へ書き換えてビルド失敗（red）を確認してから、`domain/obd.h`・`domain/obd.cpp`・`service/obdpoll.cpp`の全フィールド参照を機械的にリネームした（sedによる一括置換＋UTF-8直後で`\b`が効かなかったコメント数箇所を手動修正）。`device/ble_peripheral.cpp`は`ObdBlePacket`をopaqueな構造体として`memcpy`するだけでフィールド名を参照していなかったため変更不要だった。`OBD.md`・`CAN_REFERENCE.md`の構造体転記箇所も合わせて更新し、ついでに`OBD.md`に残っていた「obdpoll.cpp で計算」という古いコメント（`obdComputeDerived()`切り出し前の記述）も修正した。native domainテスト49件・デフォルトenvビルドとも成功。

### コードレビュー対応: CAN/OBD実装（device/can.{h,cpp}, domain/obd.{h,cpp}, service/obdpoll.{h,cpp}） **対応済み**

第三者視点でのコードレビュー（2026-08-09時点、計1,113行）を実施し、9件の指摘に対応した。総評は「device/domain/serviceの層分離が守られており、直ちに壊れるバグは無い」というもので、以下は堅牢性・精度・規格準拠面の改善。

1. **ピンコメント・文書が実装と矛盾**: `can.cpp`/`can.h`/`OBD.md`のCAN_RX_PIN/CAN_TX_PINのGPIO番号コメントが実装（TX=GPIO4=gu00Pin, RX=GPIO5=gu01Pin）と逆だったため修正。実車で動作している以上コードが正であり、コメント・文書側の誤りだった。
2. **SF/FF長の検証不足による範囲外read**: ISO-TP Single FrameのPCI下位ニブルは0-15を取りうるが正当な長さは1-7のみ。壊れたフレームで15等が来るとrx.data（8バイト配列）の範囲外を読みうる不具合を修正。First Frameのtotal lenも8バイト未満（規格上SFで送られるはず）を異常値として弾くようにした。
3. **CFの送信元IDをFF送信元に固定**: 機能アドレッシング（`0x18DB33F1`）はブロードキャストのため複数ECUが応答しうる。First Frame受信時の`rx.identifier`をラッチし、Consecutive Frameの送信元一致を要求するようにした（Mode 22でTCU等の別ECU応答が混入する前提工事）。
4. **部分失敗時にゼロが正値と区別不能**: 5リクエスト中1グループでもタイムアウトすると該当PID群が0初期値のまま下流（ログ・BLE）に流れ、本物の0と区別できなかった。`OBDReading`/`ObdBlePacket`に`validMask`（`kPids[]`配列順のPIDごとデコード成否ビットマスク）を追加。`ObdBlePacket`は末尾4バイト追加で91→95バイトになったため、mobile側`ObdReading.fromBytes()`のオフセットも追従させた。
5. **否定応答(0x7F)の非認識**: `canReceiveObdResponse()`の戻り値を`bool`から`ObdRecvResult`（`Ok`/`Timeout`/`NegativeResponse`/`Error`）に変更し、`7F [SID] [NRC]`受信時にNRCを呼び出し側へ返せるようにした（Mode 22 DIDスキャンで`7F 22 31`/`7F 22 78`の識別が必須になるための前提工事）。
6. **燃費推算にλ未反映**: `fuelRateLph`計算がλ=1固定の仮定だったが、既に取得している`commandedAfr`（=λ）を分母に反映し、暖機増量中（λ<1）の過小評価を補正した。減速フューエルカット考慮（レビュー内の修正案2）は判定ヒューリスティックが実車未検証のため見送り。
7. **millis()比較のオーバーフロー作法**: 直接比較（`millis() < deadline`等）は約49.7日でラップした際に誤動作するため、減算イディオム（`(int32_t)(millis()-deadline)`）へ変更。
8. **バッファ超過時にFC overflow未送信**: 応答長がバッファ上限を超えた際に黙って打ち切ると、ECUがN_Bsタイムアウト（規格上1秒）まで送信状態を保持し続ける。`sendFlowControl()`にflowStatus引数を追加し、FS=2（overflow）を送ってECUに即座の打ち切りを通知できるようにした。
9. **その他**: 死にコード（`can.cpp`外から呼ばれていなかった単発版`canSendObdRequest()`）を削除。`obdParseMultiResponse()`が未知PIDで打ち切る際にログを追加した。

**対象外とした指摘**: フューエルカット判定・`boost_kpa`のint16_t化検討はレビュー内でも「任意」「将来実装するなら検討」とされていた項目で、実車未検証のため今回は見送り。

### ~~TODO: CONTINUOUSモード中はOTAジョブを再チェックしない問題~~ **対応済み**

`CONTINUOUS`/`TIMED_CONTINUOUS`モードは`continuousLoopCore()`のループに留まり続け、`esp_restart()`もDeepSleepもしない限り`setup()`に戻らないため、OTAチェック（`jobsGetNext()` → `ota.handleJob()`）が`setup()`内でしか呼ばれない実装のままだと、実機がCONTINUOUS系モードで動き続けている限りOTAが永久に降ってこない問題があった（2026-08-07、実車develop ビルドで1.21.0のOTAジョブが20分経ってもQUEUEDのままだった事例で発覚）。

**対応**: `main.cpp`にJobs確認処理を`checkAndHandleJob()`として切り出し、`setup()`（起動直後）に加えて`runContinuousMode()`/`runTimedContinuousMode()`が`continuousLoopCore()`から戻るタイミング（次の5分境界ごと）でも呼ぶようにした。`notify-next`トピック購読によるプッシュ型検知も選択肢にあったが、OTAは緊急性の低い定期作業であり5分間隔の遅延は許容範囲と判断し、実装コストの低い定期ポーリング方式を採用した。

**残存課題**: OTA適用中も`blePeripheral`/`bleScanner`が動いたままのため、IPCタスクスタックオーバーフローのリスクは未解消（[OTA中のBLE無効化](CONTEXT.md#todo-ota中のble無効化ipcタスクスタックオーバーフロー対策未着手)）。CONTINUOUS系モード中にOTAを検知できるようになった分、この既知リスクが顕在化する頻度は上がる可能性がある。
