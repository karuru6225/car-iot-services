# リリース手順

## バージョン命名規則

セマンティックバージョニング（`MAJOR.MINOR.PATCH`）を使用する。

| 変更の種類 | 上げるフィールド |
|---|---|
| 後方互換性のない変更 | MAJOR |
| 機能追加（後方互換あり） | MINOR |
| バグ修正・小改善 | PATCH |

---

## リリース手順

### 1. ドキュメントを更新

以下を確認・更新する。

- `CONTEXT.md`:
  - `config.h 定数一覧` の `FIRMWARE_VERSION` 行を新バージョンに更新
  - Shadow reported ペイロード例の `fw_version` を新バージョンに更新
  - 変更内容に応じて TODO セクションを更新（完了ならステータス変更、新規なら追記）
- `ARCHITECTURE.md`: 新しいファイル・クラスを追加した場合は各テーブルを更新
- `README.md`: ハードウェア構成・リポジトリ構造・デプロイ手順に変更があれば更新

### 2. `config.h` のバージョンを更新

[src/config.h](src/config.h) の `FIRMWARE_VERSION` を新バージョンに書き換える。

```c
#define FIRMWARE_VERSION "1.16.0+" GIT_HASH  // ← 数字部分を変更
```

### 3. コミットしてタグを付ける

```bash
git add esp32_iot_gateway/src/config.h esp32_iot_gateway/CONTEXT.md esp32_iot_gateway/ARCHITECTURE.md
git commit -m "chore: FIRMWARE_VERSION を X.Y.Z に更新"
git tag vX.Y.Z
```

### 4. push する

```bash
git push origin main --tags
```

タグの push が GitHub Actions をトリガーする。

---

## GitHub Actions が自動でやること

[.github/workflows/firmware-release.yml](../.github/workflows/firmware-release.yml) が以下を順に実行する:

1. `config.h` の `FIRMWARE_VERSION` をタグのバージョンに書き換えてビルド
   - `esp32-s3-devkitc-1-release` env（`DEBUG_MODE` なし = `DEEP_SLEEP` モード）
2. `firmware.bin` を `gzip -9` で圧縮して `firmware.bin.gz` を生成
3. `firmware.bin.gz` を S3 にアップロード（`firmware/vX.Y.Z.bin.gz`）
4. OTA Job ドキュメントを生成して S3 にアップロード（`jobs/vX.Y.Z.json`）
   - Job の `url` フィールドは `.bin.gz` の S3 URL を指す
5. `ota-target-car-iot-gw` Thing グループ全台に OTA ジョブを作成
6. GitHub Release を作成して `firmware.bin` と `firmware.bin.gz` を添付

デバイスは次回起動時に OTA ジョブを検出して自動更新する。

---

## 必要な GitHub Secrets

リポジトリの **Settings → Secrets and variables → Actions → Secrets** に設定する。

| Secret 名 | 取得方法 |
|---|---|
| `AWS_ROLE_ARN` | `terraform -chdir=infra output -raw github_actions_role_arn` |
| `FIRMWARE_BUCKET` | `terraform -chdir=infra output -raw firmware_bucket` |

AWS 認証は OIDC（`v*` タグ push のみ assume 可能）。アクセスキーは不要。

---

## 注意事項

- `terraform apply`（`infra/github-actions.tf`）を先に実行していないと OIDC ロールが存在せず Actions が失敗する
- デバイスが OTA を適用するのは**次回起動時**。DeepSleep 中のデバイスはスリープ明けに自動適用される
- OTA 適用後、MQTT 接続が確認できなければ自動ロールバックする（[OTA.md](OTA.md) 参照）
- 同一バージョンへの再デプロイは OTA スキップ対象になる（`FIRMWARE_VERSION` の prefix 比較）
