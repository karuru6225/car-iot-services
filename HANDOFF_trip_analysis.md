# 引き継ぎ: OBDトリップ分析機能（trip_analysis）

対象リポジトリ: `karuru6225/car-iot-services`
背景: 別のエージェント/セッションがこの機能を修正する際に、実装の全体像を素早く把握できるようにする引き継ぎ文書。

---

## 0. 現状まとめ

**実装済み・`main`にマージ済み・実運用中**（PR #35、他に燃料列/期間フィルタ追加のPRあり）。

- バックエンド: `infra/lambda_src/trip_analysis/index.py`（Lambda 1本、テスト109件）
- インフラ: `infra/trip_analysis.tf`（Lambda・API Gateway・IAM）
- Web管理画面: `web/trip-analysis.html`
- **AIナレーティブ生成（`narrative`フィールド）も実装済み**。ただし`_process_job()`からの自動実行ではなく、
  Web管理画面のトリップ一覧（アコーディオン展開）から**特定のトリップを指定して手動実行**する方式
  （`POST /trip-analysis/narrative {device_id, key}`）。新規・過去のトリップを問わず任意のトリップに
  対して実行・再実行できる。Bedrock（Nova Pro、converse API）・位置情報（AWS Location Service・Here）・
  車固有ベースライン・z-scoreラベルを組み込んだレポートを生成する。詳細は
  `esp32_iot_gateway/CONTEXT_ARCHIVE.md`の該当TODOアーカイブ、実行可能な試作コード一式（本実装の元ネタ）は
  `experiments/trip-analysis-ai-poc/`（gitignore対象）を参照。

---

## 1. 背景と設計判断の経緯

以前は DynamoDB `device-watermark` テーブル + 1分ごとの EventBridge Scheduler ポーリングで
リアルタイムにトリップ終了を検知する設計（旧PR #28・#34、いずれも**close済み**）だった。
コードレビューで、`obd_ingest`と`trip_sweep`という別々のLambdaが同じDynamoDBアイテムを
**非アトミックにread-modify-write**しており、競合状態でトリップがサイレントに欠損しうる
致命的なバグが見つかった。1分ごとの定期実行もコスト・複雑さの割にメリットが薄いと判断し、
両PRとも設計ごと廃止した。

代わりに、**管理画面から手動で「分析開始」ボタンを押したときだけ**、以下を行う方式に転換:

1. S3に保存済みのトリップ結果一覧（追記のみの不変データ）から「どこまで分析済みか」を
   逆算する（**DynamoDB等の可変ステートを一切持たない**）
2. 分析済み範囲より後のOBDデータをAthenaで取得する
3. `obd_ts`昇順に並べ、連続する行の間隔が`GAP_TIMEOUT_SEC`を超えた箇所でトリップに分割する
4. 各トリップを集計し、S3に保存する

この設計により、旧設計の競合バグを**構造的に**回避している（可変な共有ステートが存在しないため）。

---

## 2. アーキテクチャ

### 2.1 API（Lambda 1本、`handler()`が薄いルーター）

- `POST /trip-analysis {device_id}` → `_handle_start()`
  - 前回`session_end`（無ければ`obd/`パーティションの最古タイムスタンプ）から
    `min(now, start+14日)`までのジョブを作成し、**自分自身をLambda self-invoke**
    （`InvocationType="Event"`）した直後にレスポンスを返す。
  - **API Gatewayの29秒固定タイムアウト制約を避けるため**、重い処理（Athenaクエリ〜
    トリップ分割〜集計〜複数S3書き込み）は自己呼び出し側（`_process_job`）で
    `timeout=600`秒まで自由に使う。
  - レスポンス: `{job_id, status, range, trips_saved, has_more, error}`
- `GET /trip-analysis?device_id=&job_id=` → `_handle_get()` でジョブ状態をポーリング用に返す
- `GET /trip-analysis?device_id=` → `_handle_get()` でトリップ一覧を返す（`job_id`省略時）。
  各トリップには`_load_trips()`が付与する`analysis_key`（S3キー）が含まれ、下記narrative APIが
  対象トリップを特定するのに使う
- `POST /trip-analysis/narrative {device_id, key}` → `_handle_regenerate_narrative()`。
  指定した1トリップのAIナレーティブを(再)生成し、同じS3キーへ上書き保存して更新後のトリップJSONを返す。
  `_process_job()`からは呼ばれない（自動実行しない）ため、`handler()`は`event["rawPath"]`が
  `/narrative`で終わるかで`_handle_start()`と区別してルーティングする
- 自己呼び出しイベント（`event.get("trip_analysis_job")`が真）の場合は`_process_job()`を
  直接呼び、admin権限チェック・HTTPルーティングをスキップする（内部呼び出しのため）

### 2.2 admin権限チェック

API GatewayのJWT Authorizerは**トークンの真正性のみ検証し、Cognitoグループ
（admin/viewer）までは見ない**。そのため`admin/index.py`と同じ`_is_admin()`チェックを
Lambda側でも行っている（`viewer`グループの認証済みユーザーからも呼べてしまうのを防ぐ、
defense in depth）。自己呼び出し経路はHTTPコンテキストを持たないため対象外。

### 2.3 S3キー設計

```
trip-analysis/{device_id}/year={YYYY}/month={MM}/{start:010d}_{end:010d}_v{ANALYSIS_VERSION:02d}_{seq:03d}.json
    ← トリップ集計結果（expireしない、追記のみ）
trip-analysis-jobs/{device_id}/{started_at:010d}_{job_id}.json
    ← ジョブ状態（infra/s3.tfのlifecycleルールで7日expire）
```

- トリップ集計結果は`session_end`（UTC）基準でyear/monthパーティション化
- ファイル名の`version`（`ANALYSIS_VERSION`定数、現状`1`固定）と`seq`（同一区間の再実行連番、
  `_save_trip()`が既存ファイル数から自動採番）は、**将来の「分析ロジックを直して同じ区間を
  再計算する」機能に備えた布石**。現状はどちらもほぼ固定値だが、キー構造には最初から
  含めてある
- 「前回`session_end`の逆算」（`_latest_session_end()`）は**全件リストしない**。
  `Delimiter="/"`付き`ListObjectsV2`で year→month→ファイルの3段階ドリルダウンし、
  各段階で末尾（辞書順最大＝最新）の要素だけを見る。S3のList系APIは常に辞書順で
  返るため、ゼロ埋め桁揃えのキー設計と組み合わせるとこれが機能する
- `_load_trips()`（一覧表示API用）も同様に、新しい年月・ファイルから降順に辿り
  `limit`件（デフォルト200）集まった時点で打ち切る

### 2.4 ジョブ状態管理（DynamoDB代替）

- `_find_recent_running_job()`: 直近ジョブが`RECENT_JOB_GUARD_SEC`（900秒）以内に
  `RUNNING`ならそれを返して二重起動を防止。ガード時間を超えてRUNNINGのままなら
  ワーカーが異常終了したとみなし新規ジョブを許可する**自己修復**設計（DynamoDBの
  ようなロックを持たない代わり）
- ジョブ状態ファイルは`trip-analysis-jobs/{device_id}/`配下を`job_id`でサフィックス
  検索する全件走査（`_read_job_status`）。7日でexpireするため件数は少なく問題ない

### 2.5 集計ロジック（`_compute_summary()`）

`obd_ts`昇順の行から算出:
- 距離: 連続する2点のGPS座標をhaversine公式で積算（`_haversine_m`）
- 燃料消費: `fuel_rate_lph`の台形積分
- LTFT/STFT平均、触媒温度・ブースト圧の最大値、冷却水温の始点・終点

**トリップ分割・確定ロジック（`_process_job`内）**:
- `_split_trips()`: `obd_ts`間隔が`GAP_TIMEOUT_SEC`（デフォルト600秒、環境変数で変更可）
  を超えた箇所で分割
- 末尾のトリップ候補は、現在時刻から見て`GAP_TIMEOUT_SEC`経過していなければ
  「走行中の可能性がある」として保存せず次回に持ち越す
- `MIN_TRIP_DURATION_SEC`（デフォルト30秒）未満のトリップはノイズとして破棄

### 2.6 重複コードについて

`_partition_filters` / `_run_athena_query` / `_parse_athena_results` / `_haversine_m` /
`_avg` / `_compute_summary`は、`infra/lambda_src/query/index.py`や`trip_sweep/index.py`
（DynamoDB版、closeされたPRの残骸で`main`には存在しない）とロジックが重複するが、
**このリポジトリにLambda Layer等の共有機構が無いため意図的に共通化していない**
（Rule of Three: 4つ目の類似Lambdaが必要になった時点でLayer化を検討する方針）。

---

## 3. インフラ（`infra/trip_analysis.tf`）

- Lambda: `${var.project}-trip-analysis`、`timeout=600`、既存の共有API Gateway
  （`aws_apigatewayv2_api.main`）にintegration/routeを追加する形（新規APIは作らない）
- IAM: Athena/Glueは`obd_data`テーブルのみにスコープ。S3はバケット全体への読み書き。
  self-invoke用の`lambda:InvokeFunction`は**IAMのidentity-basedポリシーのみで十分**
  （resource-basedの`aws_lambda_permission`は同一関数の自己呼び出しには不要、
  循環依存を避けるため関数名はローカル変数からリテラル組み立て）
- `infra/s3.tf`に`trip-analysis-jobs/`用の7日expireルールを追記済み

---

## 4. Web管理画面（`web/trip-analysis.html`）

- 認証ブロック（`parseJwtPayload`/`redirectToLogin`/`logout`/`apiFetch`）は`admin.html`
  から丸ごとコピー。ログイン後の戻り先は**新規Cognitoコールバック登録を避けるため
  常に`admin.html`**（`infra/cognito.tf`の`callback_urls`は`/`と`/admin.html`のみ登録済み）
- **タブUI**: `GET /admin/devices`のIoT Thing一覧（`esp32-gw-{MACフル12桁}`）から、
  `deriveObdDeviceId()`でOBD側のBLE名（`car-iot-{MAC上位6桁}`）を機械的に導出し、
  Thingごとに1タブを構築する。この変換ロジックは
  `esp32_iot_gateway/src/device/ble_scan.cpp:44`の`"car-iot-%.6s"`フォーマットに対応
- 各タブ: 「分析開始」ボタン（`POST /trip-analysis`→2秒間隔ポーリング）＋期間フィルタ
  （開始日〜終了日、クライアント側絞り込み）＋絞り込み後の合計走行距離・合計推定消費燃料表示
- **トリップ一覧はアコーディオン形式**（`.trip-item`/`.trip-header`/`.trip-detail`）。
  ヘッダー行に開始/終了/時間/距離/燃費/Grafanaリンク、クリックで展開すると詳細統計
  （LTFT/STFT平均・触媒温度最大・ブースト圧最大・点火時期平均・冷却水温・サンプル数）と
  AIナレーティブ、「AI分析を実行」ボタンが出る。開閉状態は`expandedKeys`（`analysis_key`の`Set`）
  で保持し、フィルタ再適用（`applyFilter()`）をまたいでも維持される
- 「AI分析を実行」ボタンは`POST /trip-analysis/narrative {device_id, key}`を呼び、
  レスポンスの更新後トリップJSONで`currentTrips`内の該当要素を差し替えて再描画する
  （ページ全体の再取得はしない）
- `admin.html`のヘッダーに導線リンクあり

---

## 5. テスト（`infra/lambda_src/trip_analysis/tests/`）

t-wada式TDD（Red→Green→Refactor）で全関数を実装、**109テスト**（moto + pytest + Docker）。

```bash
cd infra/lambda_src
docker compose -f docker-compose.test.yml run --build --rm test pytest trip_analysis/tests -v
```

`--build`を毎回付けること（新規ファイルがイメージに反映されないことがある、既知のハマりどころ）。

---

## 6. 別セッション継続用の補足メモ

### 6.1 実データで見つかった問題（AIナレーティブ検証中に副産物として発覚）

トリップ終端付近（エンジン停止・イグニッションOFF直前）やアイドリングストップ切替瞬間に、
`coolant_c`/`ecu_voltage`が同時にゼロになる、あるいは`absolute_load_pct`が桁外れの異常値
（5000%超）になる通信断パターンが実データで確認されている。**`coolant_c`/`ecu_voltage`の
同時ゼロは`_is_comm_dropout()`で検出し`_compute_summary()`/`_bucket_rows()`の集計から
除外済み**（この2フィールドのみの狭いスコープ、他フィールドの集計には影響させない）。
`absolute_load_pct`の桁外れ値は別問題で、モバイルアプリ側のグラフ表示はPR #36で対策済みだが、
`trip_analysis`側の集計（`_compute_summary()`は現状`absolute_load_pct`自体を扱っていない）は
未対応のまま。

**関連して2026-08-23、実運用データの調査で新たなバグを発見した**: 通信断に隣接する
時間ギャップ（BLE/CAN再接続待ちのDeepSleep由来と推定）の間、`_compute_summary()`の
距離・燃料の台形積分がその区間の燃料消費をゼロ扱いにしてしまい、ギャップ中に実移動が
あったトリップでは燃費が物理的にありえない値になる（実例: 距離2.03km・燃料0.005L・
燃費372km/L）。ほぼ全トリップで潜在的に発生しているが、通常はギャップ中ほぼ停車のため
実害が小さく気づかれていなかった。詳細・修正方針は`esp32_iot_gateway/CONTEXT.md`の
「trip_analysisの通信断ギャップで燃費が破綻するケースがある」TODO参照（**未修正、報告のみ**）。

### 6.2 AIナレーティブ生成 **実装済み（ただし自動実行ではなく個別トリップ指定の手動実行）**

当初`_process_job()`にBedrock呼び出しを直接組み込んでいたが、「新規トリップにしか
生成されない」「過去のトリップに遡って生成できない」という制約をユーザーから指摘され、
**`_process_job()`からは削除し、Web管理画面から特定のトリップを指定して呼び出す
`POST /trip-analysis/narrative {device_id, key}`（`_handle_regenerate_narrative()`→
`_regenerate_trip_narrative()`）に設計変更した**。これにより新規・過去問わず任意のトリップに
対して何度でも実行・再実行できる。`_generate_narrative()`自体（バケット化・ベースライン計算・
位置情報・Bedrock呼び出し）は変更なくそのまま流用している。

POCの設計方針はほぼそのまま踏襲したが、**1点だけ意図的に変更した**: POCの
`compute_baseline.py`等はバケット時系列のz-score母集団を過去トリップの生OBD行データを
毎回読み直して計算していたが、本実装ではAthenaへの追加クエリを避けるため、
`_compute_summary()`に保存した各トリップのavg/std/row_countを`_compute_row_baseline()`で
pooled variance公式により合成する方式にした（過去の生データを取り直さずに数学的に
同一の母集団統計を復元する）。
実装の詳細は`esp32_iot_gateway/CONTEXT_ARCHIVE.md`の該当TODOアーカイブを参照。

### 6.3 OBDのdevice_id名前空間について

OBDの`device_id`（`car-iot-xxxxxx`）とIoT Thing ID（`esp32-gw-xxxxxxxxxxxx`）は同一デバイスの
MACアドレス由来だが文字列としては別（前者はMAC上位3バイトのみ）。単純な文字列一致は
できないため、`web/trip-analysis.html`の`deriveObdDeviceId()`のような変換が必要になる
（§4参照）。この不一致は将来コード側で解消される可能性があるが、現状は変換前提。
