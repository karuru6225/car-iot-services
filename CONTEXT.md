# プロジェクトコンテキスト

## 概要

車載 IoT システム。ESP32-S3 が ADS1115 で 2 系統の車載バッテリー電圧を、
INA228 でサブバッテリーの電流・電力・温度を計測し、
SIM7080G（SORACOM Cat-M）経由で AWS IoT Core に MQTT over TLS で送信する。

クラウド側では S3 + Athena でデータを蓄積し、API Gateway + Lambda 経由で
CloudFront ホスティングの Web 管理画面からグラフ表示・削除操作を行える。

> **旧機種**: M5Atom S3 ベースの `m5atom_iot_gateway` は段階的廃止予定。
> アクティブな開発は `esp32_iot_gateway` で行う。

## データフロー

```text
ESP32-S3
  → MQTT over TLS（SIM7080G）
  → AWS IoT Core  topic: $aws/things/{device_id}/shadow/update
  → Topic Rule SQL: SELECT *, topic(3) AS device_id FROM '$aws/things/+/shadow/update'
  → Lambda ingest
  → S3: raw/year=YYYY/month=MM/day=DD/hour=HH/{device_id}-{uuid8}.json

Web 管理画面
  → GET /data?hours=24  → Lambda query → Athena（非同期ポーリング）→ S3
      ※ 各行に s3_key（Athena $path 疑似カラム）を含む
  → DELETE /data  body: {"s3_keys": [...]}  → Lambda delete → S3 直接削除
  ※ 認証: Cognito JWT（API Gateway JWT Authorizer）、Hosted UI でログイン
  ※ タイムスタンプは JST 表示（Web 側で変換）
```

## MQTT ペイロード形式

### デバイスシャドウ更新（esp32_iot_gateway）

トピック: `$aws/things/{device_id}/shadow/update`

```json
{"state":{"reported":{"v1":12.34,"v2":12.10,"current":5.2100,"power":62.500,"temp":28.5,"ts":1746143400}}}
```

| フィールド | 型 | 内容 |
| --- | --- | --- |
| `v1` | float | サブバッテリー電圧（V） |
| `v2` | float | メインバッテリー電圧（V） |
| `current` | float | サブバッテリー電流（A） |
| `power` | float | サブバッテリー電力（W） |
| `temp` | float | INA228 内蔵温度センサー（°C） |
| `ts` | int | UNIX タイムスタンプ（秒） |

### 旧形式（m5atom_iot_gateway、段階的廃止予定）

```json
{"type":"battery","id":"voltage_1","voltage":12.34,"fw":"1.0.0+a364343b","ts":"2026-03-05T12:00:00Z"}
{"type":"thermometer","addr":"AA:BB:CC:DD:EE:FF","temp":25.0,"humidity":60,"battery":80,"rssi":-70,"mf":"09b0e9...","fw":"1.0.0+a364343b","ts":"2026-03-05T12:00:00Z"}
{"type":"co2meter","addr":"AA:BB:CC:DD:EE:FF","temp":25.0,"humidity":60,"co2":612,"battery":80,"rssi":-70,"mf":"09b0e9...","fw":"1.0.0+a364343b","ts":"2026-03-05T12:00:00Z"}
```

## S3 / Glue テーブルスキーマ

| カラム | 型 | 説明 |
| --- | --- | --- |
| `ts` | string | ISO8601 UTC タイムスタンプ |
| `type` | string | `"battery"` / `"thermometer"` / `"co2meter"`（デバイス送信値そのまま） |
| `device_id` | string | IoT Thing 名（topic から抽出） |
| `voltage` | double | 電圧（V） |
| `id` | string | 電圧センサー識別子（`"voltage_1"` 等） |
| `addr` | string | BLE MAC アドレス |
| `temp` | double | 温度（°C） |
| `humidity` | int | 湿度（%） |
| `battery` | int | BLE デバイスバッテリー（%） |
| `rssi` | int | RSSI（dBm） |
| `co2` | int | CO2 濃度（ppm）※ co2meter のみ |
| `mf` | string | Manufacturer Data hex 文字列（デバッグ用） |
| `fw` | string | ファームウェアバージョン（例: `"1.0.0+a364343b"`） |
| `year/month/day/hour` | string | Hive パーティション（プロジェクション） |

## 重要な設計決定

### Athena 非同期クエリ（query Lambda）

1. `GET /data?hours=24[&type=battery|thermometer|co2meter]` で execution_id を返す
2. Web 側が 2 秒ポーリングで `GET /data?execution_id=xxx` を叩く
3. `SUCCEEDED` になったら結果を返す。各行に `s3_key`（Athena の `$path` 疑似カラム）を含む

### 行単位削除（delete Lambda）

`DELETE /data` に JSON body `{"s3_keys": ["s3://bucket/raw/..."]}` を渡すと、
Athena を使わず指定キーを直接 S3 削除する。
クエリ結果の `s3_key` を使うため、選択した行だけを正確に削除できる。

### LTE / MQTT（SIM7080G + AWS IoT Core）

- SIM7080G 内蔵 MQTT クライアント（AT+SMCONN / AT+SMPUB）を使用
- TLS 証明書は SPIFFS に保存（`/certs/ca.crt` 等）、起動時に CRC チェックして変更時のみモデムへ再アップロード
- DeepSleep 前に必ず `lte.disconnect()`（MQTT+GPRS 切断）を呼ぶ
- APN 設定は `CFUN=0 → CGDCONT → CFUN=1` の順（必須）
- `modem.restart()` は使わず `modem.init()` を使う

## 将来の拡張候補

- **BLE スキャン**: 基盤実装済み（`device/ble_scan`・`domain/sensor*`）。MQTT 送信は未有効化（`main.cpp` でコメントアウト中）
- **コマンド受信**: AWS IoT Core から MQTT でリレー制御・設定変更を受信
- **Web 管理画面**: デバイス設定のリモート管理
- **DDoS・コスト対策強化**（不特定多数公開時）:
  - API も CloudFront 経由に統一し、API Gateway に Resource Policy で直接アクセスを禁止
  - CloudFront に AWS WAF を付けてレートリミットを適用（$5/月〜）
  - 現状は API GW スロットリングのみ適用（個人用途では十分）
