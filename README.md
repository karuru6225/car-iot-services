# car-iot-services

車載 IoT システム。ESP32-S3 が車載バッテリー電圧・電流・電力を計測し、
SORACOM Cat-M 経由で AWS IoT Core に送信する。
クラウド側では S3 + Athena でデータを蓄積し、CloudFront ホスティングの Web 管理画面でグラフ表示・削除操作を行える。

## システム構成

```text
ESP32-S3-MINI-1 (esp32_iot_gateway)
  ├── ADS1115 で 2 系統バッテリー電圧測定（サブ / メイン）
  ├── INA228 でサブバッテリー電流・電力・温度測定
  └── BLE スキャンで SwitchBot センサー（温湿度計 / CO2）受信
        ↓ MQTT over TLS
  SIM7080G (SORACOM Cat-M)
        ↓
  AWS IoT Core
  ├── Topic Rule → Lambda (ingest) → S3 (raw/year=.../month=.../day=.../hour=.../)
  │                                         ↓ Glue + Athena
  │                                   API Gateway → Lambda (query / delete)
  │                                         ↑ CloudFront → Web 管理画面 (index.html)
  ├── Shadow (desired/delta) → デバイス設定変更（充電制御・閾値等）
  └── IoT Jobs → OTA ファームウェア更新
```

## ハードウェア

| 項目 | 内容 |
| ---- | ---- |
| MCU | ESP32-S3-MINI-1-N8（`m5atom_power_adc` 基板直付け） |
| ADC | ADS1115（I2C: SDA=GPIO17, SCL=GPIO18, ADDR=0x48） |
| 電流計 | INA228（I2C: SDA=GPIO17, SCL=GPIO18, ADDR=0x40） |
| OLED | SSD1306 128×64（I2C: SDA=GPIO17, SCL=GPIO18, ADDR=0x3C） |
| BLE | SwitchBot WoIOSensor（温湿度計）/ CO2センサー（アドバタイズ受信） |
| ボタン | GPIO26（Btn0）/ GPIO33（Btn1） |
| LTE | M5Stack U128（SIM7080G CAT-M/NB-IoT）、SORACOM SIM |
| 電源 | 車載 12V → LM2596（12V→5V）→ AMS1117-3.3（5V→3.3V）→ ESP32 |

## リポジトリ構成

```text
car-iot-services/
├── esp32_iot_gateway/     ESP32-S3-MINI-1 ゲートウェイ（アクティブ）
│   ├── ARCHITECTURE.md    レイヤー構成・依存ルール
│   ├── CONTEXT.md         実装詳細・設計決定・TODO
│   ├── OTA.md             OTA 仕様
│   ├── RELEASE.md         リリース手順（GitHub Actions）
│   └── MENU.md            OLED メニュー仕様
├── m5atom_iot_gateway/    M5Atom S3 ゲートウェイ（段階的廃止予定）
├── m5atom_power_adc/      新 PCB KiCad プロジェクト（電源・ADC・リレー・ESP32-S3 直付け）
│   ├── CIRCUIT.md         回路設計仕様書
│   └── *.kicad_sch        階層シート構成（メイン + GroveUnit / RelayControl / VoltageSense）
├── infra/                 クラウドインフラ（Terraform）
│   ├── manage.ps1         デプロイスクリプト（plan / apply）
│   └── lambda_src/        Lambda ソースコード
├── ops/                   運用スクリプト
│   ├── provision_device.ps1 / .sh  ESP32 初回プロビジョニング（証明書発行・SPIFFS 書き込み）
│   ├── deploy_ota.ps1 / .sh        OTA 手動デプロイ（GitHub Actions 利用推奨）
│   ├── gen_certs.ps1      証明書生成
│   └── send_command.ps1   IoT Jobs コマンド送信
├── web/
│   └── index.html         Web 管理画面（単一ファイル SPA）
├── docs/                  調査・設計メモ
├── tools/                 開発補助ツール（KiCad MCP サーバー等）
├── rtx830_filter_updater/ RTX830 フィルタ更新スクリプト
├── ARCHITECTURE.md
├── CONTEXT.md             開発引き継ぎ資料
├── HARDWARE.md            新 PCB ハードウェア設計仕様
└── SIM7080G.md            SIM7080G AT コマンドリファレンス
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

PlatformIO でビルド・書き込み。`esp32_iot_gateway/` を参照。

### 初回プロビジョニング

```powershell
cd ops
.\provision_device.ps1 -Port COM3
# AWS プロファイルを指定する場合
.\provision_device.ps1 -Port COM3 -Profile myprofile
```

スクリプトが MAC アドレスからデバイス ID（`esp32-gw-xxxxxxxxxxxx`）を生成し、AWS IoT Core に Thing を登録・証明書を発行・SPIFFS に書き込む。

その後、**AWS IoT Core コンソール → Thing グループ `ota-target-car-iot-gw` → デバイス ID を追加**することで OTA の配信対象になる。グループに入っていないデバイスには OTA ジョブが届かない。

### OTA リリース

`vX.Y.Z` タグを push すると GitHub Actions が自動でビルド・S3 アップロード・IoT Job 作成・GitHub Release を実行する。

```powershell
git tag v1.2.3
git push origin main --tags
```

詳細は [esp32_iot_gateway/RELEASE.md](esp32_iot_gateway/RELEASE.md) および [esp32_iot_gateway/OTA.md](esp32_iot_gateway/OTA.md) を参照。
