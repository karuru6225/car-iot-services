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
  → AWS IoT Core  topic①: sensors/{device_id}/data  （バッテリーテレメトリ + BLE センサー）
                  topic②: $aws/things/{device_id}/shadow/update  （デバイス設定値の reported）
  → Topic Rule: SELECT *, topic(2) AS device_id FROM 'sensors/+/data'
    → Lambda ingest → S3: raw/.../{device_id}-{uuid8}.json

クラウド → デバイス（リモート設定変更）
  → Shadow desired を更新 → delta トピック → デバイスが NVS に適用

Web 管理画面（index.html）
  → GET /data?hours=24  → Lambda query → Athena（非同期ポーリング）→ S3
      ※ 各行に s3_key（Athena $path 疑似カラム）を含む
  → DELETE /data  body: {"s3_keys": [...]}  → Lambda delete → S3 直接削除
  → GET /status  → Lambda status → IoT Shadow（デバイス設定値の現在値）
  ※ 認証: Cognito JWT（API Gateway JWT Authorizer）、Hosted UI でログイン
  ※ タイムスタンプは JST 表示（Web 側で変換）

Admin コンソール（admin.html、admin グループユーザーのみ）
  → GET /admin/devices      → 全 esp32-gw-* デバイス一覧 + Shadow reported + Thing Groups
  → PUT /admin/shadow/{id}  → Shadow desired 更新（chg_start_v, chg_stop_v 等）
  → POST /admin/command/{id} → IoT Job 発行（ah_reset / charge_main_batt）
  → PUT /admin/groups/{id}  → Thing Group メンバーシップ変更
```

## MQTT ペイロード形式

### バッテリーテレメトリ（esp32_iot_gateway）

トピック: `sensors/{device_id}/data`

```json
{"type":"battery","main":12.34,"sub":12.10,"current":5.2100,"power":62.500,"temp":28.5,"ah":200.001234,"ts":1746143400}
```

| フィールド | 型 | 内容 |
| --- | --- | --- |
| `type` | string | `"battery"` 固定 |
| `main` | float | メインバッテリー電圧（V） |
| `sub` | float | サブバッテリー電圧（V） |
| `current` | float | サブバッテリー電流（A） |
| `power` | float | サブバッテリー電力（W） |
| `temp` | float | INA228 内蔵温度センサー（°C） |
| `ah` | float | 積算電荷量（Ah）= INA228 積算値 + Ah オフセット |
| `ts` | int | UNIX タイムスタンプ（秒） |

### デバイスシャドウ（設定値 reported）

トピック: `$aws/things/{device_id}/shadow/update`

```json
{"state":{"reported":{"ah_offset":200,"relay_mode":"sleep_indicator","chg_start_v":11.70,"chg_stop_v":12.50,"chg_duration_sec":1800,"fw_version":"1.9.1+fd8bb3e0"}}}
```

### デバイスシャドウ（リモート設定変更）

クラウドから `desired` を更新するとデバイスが次回起動時に delta を受け取り適用する。

```json
{"state":{"desired":{"ah_offset":200}}}
{"state":{"desired":{"relay_mode":"off"}}}
{"state":{"desired":{"chg_start_v":11.5,"chg_stop_v":12.8,"chg_duration_sec":3600}}}
```

対応フィールド:

- `ah_offset`（int）— Ah オフセット
- `relay_mode`（`"sleep_indicator"` or `"off"`）— リレー動作モード
- `chg_start_v`（float）— 自動充電開始電圧（V）、デフォルト 11.7
- `chg_stop_v`（float）— 自動充電停止電圧（V）、デフォルト 12.5
- `chg_duration_sec`（uint）— 自動充電継続時間（秒）、デフォルト 1800

### 旧形式（m5atom_iot_gateway、段階的廃止予定）

```json
{"type":"thermometer","addr":"AA:BB:CC:DD:EE:FF","temp":25.0,"humidity":60,"battery":80,"rssi":-70,"mf":"09b0e9...","fw":"1.0.0+a364343b","ts":"2026-03-05T12:00:00Z"}
{"type":"co2meter","addr":"AA:BB:CC:DD:EE:FF","temp":25.0,"humidity":60,"co2":612,"battery":80,"rssi":-70,"mf":"09b0e9...","fw":"1.0.0+a364343b","ts":"2026-03-05T12:00:00Z"}
```

## S3 / Glue テーブルスキーマ

| カラム | 型 | 説明 |
| --- | --- | --- |
| `ts` | string | ISO8601 UTC タイムスタンプ |
| `type` | string | `"battery"` / `"thermometer"` / `"co2meter"` |
| `device_id` | string | IoT Thing 名（topic から抽出） |
| `main` | double | メインバッテリー電圧（V）※ battery のみ |
| `sub` | double | サブバッテリー電圧（V）※ battery のみ |
| `current` | double | 電流（A）※ battery のみ |
| `power` | double | 電力（W）※ battery のみ |
| `ah` | double | 積算電荷量（Ah）※ battery のみ |
| `addr` | string | BLE MAC アドレス ※ thermometer / co2meter のみ |
| `temp` | double | 温度（°C） |
| `humidity` | int | 湿度（%）※ thermometer / co2meter のみ |
| `battery` | int | BLE デバイスバッテリー（%）※ thermometer / co2meter のみ |
| `rssi` | int | RSSI（dBm）※ thermometer / co2meter のみ |
| `co2` | int | CO2 濃度（ppm）※ co2meter のみ |
| `voltage` | double | 電圧（V）※ 旧 m5atom battery 形式のみ |
| `id` | string | 電圧センサー識別子 ※ 旧 m5atom battery 形式のみ |
| `mf` | string | Manufacturer Data hex ※ 旧 m5atom 形式のみ |
| `fw` | string | ファームウェアバージョン ※ 旧 m5atom 形式のみ |
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

- **BLE スキャン**: 基盤実装済み（`device/ble_scan`・`domain/sensor*`）。MQTT 送信も有効化済み
- **Jobs コマンド**: `ah_reset` 実装済み。リレー制御（`relay_on` / `relay_off`）は未実装
- **Web からのリモート設定**: Shadow desired 更新は Console/CLI から可能。Web ダッシュボードの設定 UI は未実装
- **DDoS・コスト対策強化**（不特定多数公開時）:
  - API も CloudFront 経由に統一し、API Gateway に Resource Policy で直接アクセスを禁止
  - CloudFront に AWS WAF を付けてレートリミットを適用（$5/月〜）
  - 現状は API GW スロットリングのみ適用（個人用途では十分）
