# car-iot-services

車載 IoT システム。M5Atom S3 が車載バッテリー電圧・SwitchBot BLE センサーのデータを収集し、
SORACOM Cat-M 経由で AWS IoT Core に送信する。
クラウド側では S3 + Athena でデータを蓄積し、CloudFront ホスティングの Web 管理画面でグラフ表示・削除操作を行える。

## システム構成

```
M5Atom S3 (ESP32-S3)
  ├── ADS1115 で車載バッテリー電圧測定
  └── SwitchBot BLE センサースキャン（温湿度計 / CO2センサー）
        ↓ MQTT over TLS
  SIM7080G (SORACOM Cat-M)
        ↓
  AWS IoT Core  topic: sensors/{device_id}/data
        ↓ Topic Rule
  Lambda (ingest) → S3 (raw/year=.../month=.../day=.../hour=.../)
                          ↓
                    Glue + Athena
                          ↓
  API Gateway → Lambda (query / delete)
                          ↑
                  CloudFront → Web 管理画面 (index.html)
```

## ハードウェア

| 項目 | 内容 |
|---|---|
| MCU | M5Atom S3（ESP32-S3） |
| ADC | ADS1115（I2C: SDA=G38, SCL=G39, ADDR=0x49）|
| センサー | SwitchBot WoIOSensor（温湿度計）/ CO2センサー |
| LTE | M5Stack U128（SIM7080G）、SORACOM SIM |
| 電源 | 車載 12V → DC-DC → M5Atom S3 |

## リポジトリ構成

```
car-iot-services/
├── m5atom_iot_gateway/   デバイス側ファームウェア（PlatformIO）
├── m5atom_power_adc/     新 PCB KiCad プロジェクト（電源・ADC・リレー・ESP32-S3 直付け）
│   ├── CIRCUIT.md        回路設計仕様書
│   └── *.kicad_sch       階層シート構成（メイン + GroveUnit / RelayControl / VoltageSense）
├── infra/                クラウドインフラ（Terraform）
│   ├── manage.ps1        デプロイスクリプト
│   ├── gen_certs.ps1     証明書生成スクリプト（Secrets Manager → certs.h）
│   └── lambda_src/       Lambda ソースコード
├── web/
│   └── index.html        Web 管理画面（単一ファイル SPA）
├── docs/                 調査・設計メモ
├── tools/                開発補助ツール（KiCad MCP サーバー等）
├── rtx830_filter_updater/ RTX830 フィルタ更新スクリプト
├── ARCHITECTURE.md
├── CONTEXT.md            開発引き継ぎ資料
├── HARDWARE.md           新 PCB ハードウェア設計仕様
└── SIM7080G.md           SIM7080G AT コマンドリファレンス
```

## デプロイ

### 前提

- Terraform >= 1.5
- AWS CLI（認証済み）
- 以下のファイルを `infra/` に配置（gitignore 済み）:
  - `backend.tfbackend` — S3 バックエンド設定
  - `terraform.tfvars` — 変数定義（`hosted_zone_id`, `web_subdomain` 等、`terraform.tfvars.example` を参照）

### インフラ + Lambda

`infra/` ディレクトリで PowerShell を実行:

```powershell
# 変更確認
.\manage.ps1 plan

# 適用（確認プロンプトあり）
.\manage.ps1 apply

# 確認なしで即適用
.\manage.ps1 apply -AutoApprove

# AWS プロファイルを指定する場合
.\manage.ps1 apply -Profile myprofile
```

Lambda コード（`lambda_src/`）の変更も `apply` で自動デプロイされる（`archive_file` の `source_code_hash` で変更検知）。

### Web 管理画面

`index.html` は `aws_s3_object` で Terraform 管理されているため、`manage.ps1 apply` で自動アップロードされる。
API エンドポイント・Cognito 設定はデプロイ時に Terraform が HTML 内のプレースホルダーを置換して埋め込む。

CloudFront のキャッシュ TTL はデフォルト 300 秒。URL は apply 後に確認できる:

```powershell
cd infra
terraform output web_url
```

## デバイスファームウェア

PlatformIO でビルド・書き込み。`m5atom_iot_gateway/` を参照。

```powershell
# terraform apply 後に実行すると certs.h が自動生成される
cd infra
.\gen_certs.ps1 -Profile myprofile
```

詳細は [CONTEXT.md](CONTEXT.md) を参照。
