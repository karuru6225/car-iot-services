# 完了TODOアーカイブ

`CONTEXT.md` の「作業中・引き継ぎ事項」セクションから、実装済み/対応済みになったTODO、および対応しない方針に決めたTODOをここに移した。現在進行中のTODOは `CONTEXT.md` を参照。

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

新規動作モード `OperationMode::CONTINUOUS_OBD` を追加。`device/can.h/.cpp`（TWAI・29bit拡張アドレッシング・N-VAN対応）、`domain/obd.h/.cpp`（全28PIDデコード）、`service/obdpoll.h/.cpp`（1秒間隔逐次ポーリング）を新規実装。取得値はOLED表示（`oledShowObdData()`）とBLE Notify（`MEAS_OBD_UUID`、87バイトを18バイトずつ5チャンクに分割）でスマホアプリ（`mobile/lib/main.dart`）へ送信する。Shadowの`override_next_mode="continuous_obd"`とメニューの`"Continuous OBD"`の両方から起動できる。

AWS への publish（`domain/telemetry`統合）は今回のスコープ外で未実装。詳細（実車スキャン結果・CANプロトコル詳細・ビットマスク等）は `OBD.md` 参照。

### ~~TODO: OTA失敗時にAWS IoT Jobsへ FAILED を報告しない~~ **実装済み**

`Ota::apply()`（[ota.cpp:154-232](esp32_iot_gateway/src/service/ota.cpp#L154-L232)）の全ての失敗パス（ダウンロード失敗・`esp_ota_begin`失敗・書き込み失敗・`esp_ota_end`失敗・boot partition設定失敗）が`false`を返すだけで`jobsReport(job.id, "FAILED", ...)`を呼ばない。`handleJob()`は既に`IN_PROGRESS`を送信済み（[ota.cpp:333](esp32_iot_gateway/src/service/ota.cpp#L333)）だが、失敗時に`FAILED`が送られないため、AWS側でJobが`IN_PROGRESS`のまま永久に残り続ける。呼び出し元の`main.cpp`も`ota.handleJob(job)`の戻り値を見ていない。

**実装方針（案）**: `apply()`の各失敗パスに`jobsReport(jobId, "FAILED", <理由>)`を追加する。`apply()`内で`jobId`を保持しているので、失敗理由（ダウンロード失敗/書き込み失敗/検証失敗等）を`statusDetails.reason`に含めれば運用側の切り分けにも使える。

### ~~TODO: Shadowのoverride_next_modeが起動直後しか反映されない~~ **対応しない方針**

`getShadowOverrideMode()`は`setup()`内で1回だけ呼ばれる（`main.cpp`）。一方`shadowPollDelta()`は`continuousLoopCore()`の1秒ティックや`enterDeepSleepMode()`、`loop()`本体でも呼ばれ、そこで`override_next_mode`のdeltaを受け取ると`s_overridePending`をセットしAWSへACK（`shadowPublishConfig(true)`）まで返してしまう（`shadow.cpp`）が、`s_overridePending`を実際にモードへ反映する`getShadowOverrideMode()`の呼び出しは起動時の1回しかないため、稼働中に送ったoverride_next_modeは「reportedは更新されたのに実際のモードは変わらない」という状態になる。

**判断**: 現状の挙動（起動直後のみ反映）のまま許容することにした。`override_next_mode`は元々「次回起動時に1サイクルだけ」という起動タイミング前提の機能（`ONE_SHOT_CONTINUOUS`）であり、稼働中に送っても即座に反映される必要性は薄いと判断。
