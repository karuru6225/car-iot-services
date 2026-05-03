# システム全体構成

```text
┌──────────────────────────────────────────────────────────────┐
│  デバイス層                                                    │
│  esp32_iot_gateway/   (ESP32-S3-MINI-1 + SIM7080G)           │
│    OTA チェック（AWS IoT Jobs）→ MQTT over TLS → AWS IoT Core │
│  m5atom_iot_gateway/  (M5Atom S3 — 段階的廃止予定)           │
│    電圧測定・BLE スキャン → MQTT over TLS → AWS IoT Core      │
├──────────────────────────────────────────────────────────────┤
│  クラウド層 (infra/)                                           │
│  IoT Core → Topic Rule → Lambda(ingest) → S3(data)           │
│  IoT Jobs → OTA 更新ジョブ管理 → デバイス → S3(firmware) DL  │
│  API GW → Lambda(query/delete) → Athena → S3                 │
│  CloudFront → S3(web) → Web 管理画面                          │
└──────────────────────────────────────────────────────────────┘
```

## クラウドインフラ（infra/）

Terraform で管理。主要リソース：

| リソース | 役割 |
| --- | --- |
| AWS IoT Core Thing + 証明書 + Policy | デバイス認証・MQTT エンドポイント（ポリシーはデバイス共通・ワイルドカード） |
| IoT Topic Rule | MQTT メッセージを Lambda ingest に転送 |
| AWS IoT Jobs | OTA 更新ジョブのキュー管理・状態追跡（QUEUED→IN_PROGRESS→SUCCEEDED/FAILED） |
| Lambda `ingest` | JSON を S3 に保存（パーティション付き） |
| S3 バケット（データ用） | `raw/year=/month=/day=/hour=/` 階層構造（非公開） |
| S3 バケット（ファームウェア配布用） | OTA firmware.bin 配置（証明書をファームに含まないため公開可） |
| Glue Database + Table | S3 データのスキーマ定義・パーティションプロジェクション |
| Athena Workgroup | SQL クエリ実行 |
| Lambda `query` | Athena 非同期クエリ発行・結果取得 |
| Lambda `delete` | `s3_keys` 直接削除 or Athena で対象特定 → S3 オブジェクト削除 |
| Cognito User Pool + App Client | ユーザー認証・JWT 発行（Hosted UI） |
| API Gateway JWT Authorizer | Cognito JWT トークン検証 |
| API Gateway HTTP API | `GET /data`, `DELETE /data` |
| S3 バケット（Web 用） + CloudFront + Route53 | カスタムドメインで管理画面ホスティング |
| ACM 証明書（us-east-1） | CloudFront 用 TLS 証明書 |

## Web 管理画面（web/index.html）

- **単一ファイル**の静的 SPA（CloudFront + S3 で配信、`aws_s3_object` で Terraform 管理）
- Chart.js 4 で電圧・温度・湿度・CO2 を独立したグラフで表示（アコーディオン切り替え）
- グラフ間でホバー縦線を同期（カスタム Chart.js プラグイン + AbortController）
- API エンドポイント・Cognito 設定は Terraform デプロイ時に HTML へ直接埋め込み（プレースホルダー置換）
- ラベル設定（addr/id の表示名）は `localStorage` に保存
- 認証トークン（id_token）は `sessionStorage` に保存、未ログイン時は Cognito Hosted UI にリダイレクト

## デバイス側アーキテクチャ

各デバイスの詳細は個別の ARCHITECTURE.md を参照：

- `esp32_iot_gateway/ARCHITECTURE.md` — 3 層構成（device / domain / service）・依存ルール・命名規則
- `m5atom_iot_gateway/ARCHITECTURE.md` — 4 層構成（domain / infra / app / presentation）・依存ルール・include 順序規則
