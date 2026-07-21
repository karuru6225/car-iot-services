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

[.github/workflows/firmware-release.yml](../.github/workflows/firmware-release.yml) が **v1/v2 基板それぞれ**に対して以下を順に実行する（`strategy.matrix` で `board: [1, 2]` を並列実行）:

1. `config.h` の `FIRMWARE_VERSION` をタグのバージョンに書き換えてビルド
   - `esp32-s3-devkitc-1-v{board}-release` env（`DEBUG_MODE` なし = `DEEP_SLEEP` モード）
2. `firmware.bin` を `gzip -9` で圧縮して `firmware.bin.gz` を生成
3. `firmware.bin.gz` を S3 にアップロード（`firmware/v{board}/vX.Y.Z.bin.gz`）
4. OTA Job ドキュメントを生成して S3 にアップロード（`jobs/v{board}/vX.Y.Z.json`）
   - `url` フィールドは `.bin.gz` の S3 URL、`board_version` フィールドに基板番号（1 or 2）を含む
5. `ota-target-car-iot-gw-v{board}` Thing グループに OTA ジョブを作成（基板ごとに別Job）
6. GitHub Release を作成して両基板分の `firmware.bin` / `firmware.bin.gz`（計4ファイル）を添付

デバイスは自分が所属する Thing Group 宛の Job しか受け取らない。さらに `ota.cpp::handleJob()` が Job ドキュメントの `board_version` と NVS の `getBoardVersion()` を比較し、不一致なら `FAILED` として適用をスキップする（Thing Group誤登録に対する保険）。

デバイスは次回起動時に OTA ジョブを検出して自動更新する。

---

## 基板追加・移行時の注意

### 新規デバイスのprovisioning

`provision_device.ps1 -BoardVersion {1|2}` が Thing 作成と同時に `ota-target-car-iot-gw-v{BoardVersion}` へ自動登録する。手動でのGroup登録は不要。

### 既存デバイスの移行（本パイプライン導入時の一度きりの作業）

このOTA基板別パイプライン導入前から稼働している実機は、旧Thing Group `ota-target-car-iot-gw` に手動登録されている想定。`infra/iot.tf` の `terraform apply` 後、該当デバイスを新Groupへ再登録する:

```bash
aws iot add-thing-to-thing-group --thing-group-name ota-target-car-iot-gw-v1 --thing-name <device-id>
```

### 初回移行時の無防備ウィンドウ（重要）

`board_version` 整合性チェックはOTAで配られるファームウェアコード自体の一部のため、**このパイプライン導入後に最初に配信されるJobを受け取る時点では、実機はまだ旧ファーム（チェックなし）で動いている**。この1回に限り、Thing Group誤登録があっても検出されずそのまま誤ったバイナリが書き込まれてしまう。

- **対策**: このパイプライン導入後、最初のリリースタグをpushする前に `aws iot list-things-in-thing-group --thing-group-name ota-target-car-iot-gw-v1`（v2も同様）でメンバー登録を目視確認する
- 2回目以降のOTAからは、デバイス側に`board_version`チェックが既に乗っているため、Group誤登録があってもFAILEDで安全に弾かれる

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
- `board_version` 不一致の場合は Job が `FAILED`（理由: `board_version mismatch`）になる。バージョン一致スキップより先に評価される
