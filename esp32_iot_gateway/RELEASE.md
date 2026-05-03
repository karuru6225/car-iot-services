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

### 1. `config.h` のバージョンを更新

[src/config.h](src/config.h) の `FIRMWARE_VERSION` を新バージョンに書き換える。

```c
#define FIRMWARE_VERSION "1.3.1+" GIT_HASH  // ← 数字部分を変更
```

### 2. コミットしてタグを付ける

```bash
git add esp32_iot_gateway/src/config.h
git commit -m "chore: FIRMWARE_VERSION を X.Y.Z に更新"
git tag vX.Y.Z
```

### 3. push する

```bash
git push origin main --tags
```

タグの push が GitHub Actions をトリガーする。

---

## GitHub Actions が自動でやること

[.github/workflows/firmware-release.yml](../.github/workflows/firmware-release.yml) が以下を順に実行する:

1. `config.h` の `FIRMWARE_VERSION` をタグのバージョンに書き換えてビルド
   - `esp32-s3-devkitc-1-release` env（`DEBUG_MODE` なし = `DEEP_SLEEP` モード）
2. `firmware.bin` を S3 にアップロード（`firmware/vX.Y.Z.bin`）
3. IoT Job ドキュメントを S3 にアップロード（`jobs/vX.Y.Z.json`）
4. `esp32-gw-*` の Thing 全台に OTA ジョブを作成
5. GitHub Release を作成して `firmware.bin` を添付

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
