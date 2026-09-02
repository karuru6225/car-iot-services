# リリース手順

## バージョン命名規則

`MAJOR.MINOR.PATCH` 形式だが、**MAJORは基板シリーズを恒久的に表す** — 標準的なsemverの「MAJOR=破壊的変更」とは異なる、本プロジェクト独自の規約:

| フィールド | 意味 |
|---|---|
| MAJOR | 基板シリーズ固定。`1` = v1基板、`2` = v2基板。**変更に関わらず一切上げない** |
| MINOR | シリーズ内の機能追加（後方互換あり） |
| PATCH | シリーズ内のバグ修正・小改善 |

v1系とv2系は独立してバージョンが進む（例: v1系が `1.19.0` の頃にv2系は `2.4.0` ということがあり得る）。これにより「v1は保守モードでリリース頻度を落とす、v2は新機能を出し続ける」といった独立したリリースサイクルが可能になる。

**`config.h`（ソース）が正、gitタグは単なるトリガー**: `src/config.h` の `FIRMWARE_VERSION_BASE`（`#if BOARD_VERSION == 1/2` で分岐、[src/config.h](src/config.h)参照）が実際にビルドされる版数の唯一の情報源。gitタグはGitHub Actionsを発火させ、GitHub Releaseの表示名になるだけの役割で、ビルドされる版数を決めるものではない。タグの数字と`config.h`の値が一致しない場合、CIは検証エラーで停止する（後述）。

---

## リリース手順

まず変更が **片方のシリーズのみに影響するか、両シリーズ共通か** を判断する。

### 1. ドキュメントを更新

以下を確認・更新する。

- `CONTEXT.md`:
  - `config.h 定数一覧` の `FIRMWARE_VERSION` 行を新バージョンに更新
  - Shadow reported ペイロード例の `fw_version` を新バージョンに更新
  - 変更内容に応じて TODO セクションを更新（完了ならステータス変更、新規なら追記）
- `ARCHITECTURE.md`: 新しいファイル・クラスを追加した場合は各テーブルを更新
- `README.md`: ハードウェア構成・リポジトリ構造・デプロイ手順に変更があれば更新

### 2. `config.h` のバージョンを更新

[src/config.h](src/config.h) の `FIRMWARE_VERSION_BASE` を、変更対象のシリーズの分岐だけ書き換える（MAJORは変更しない。もう片方のシリーズの行はそのまま）。

```c
#if BOARD_VERSION == 1
#define FIRMWARE_VERSION_BASE "1.19.0" // FIRMWARE_VERSION_V1  ← ここだけ変更（v1系の変更の場合）
#elif BOARD_VERSION == 2
#define FIRMWARE_VERSION_BASE "2.0.0" // FIRMWARE_VERSION_V2
#endif
```

### 3. コミットしてタグを付ける

**この後に付けるタグの数字は、必ず上記で書いた `FIRMWARE_VERSION_BASE` と一字一句一致させる**（一致しないとCIが検証エラーで停止する）。

**片方のシリーズのみの変更**（例: v1固有のバグ修正）:

```bash
git add esp32_iot_gateway/src/config.h esp32_iot_gateway/CONTEXT.md esp32_iot_gateway/ARCHITECTURE.md
git commit -m "chore: FIRMWARE_VERSION を 1.19.0 に更新"
git tag v1.19.0
git push origin main --tags
```

**両シリーズ共通の変更**（例: MQTT処理の共通バグ修正）— 各シリーズの次バージョンをそれぞれ決めて`config.h`の両方の分岐を更新し、同一コミットに両方のタグを付ける:

```bash
git add esp32_iot_gateway/src/config.h esp32_iot_gateway/CONTEXT.md esp32_iot_gateway/ARCHITECTURE.md
git commit -m "chore: FIRMWARE_VERSION を更新（v1: 1.19.0, v2: 2.4.0）"
git tag v1.19.0
git tag v2.4.0
git push origin main --tags
```

タグのpushが（タグごとに独立して）GitHub Actions をトリガーする。1タグ = 1基板のビルド・リリースになる。

---

## GitHub Actions が自動でやること

[.github/workflows/firmware-release.yml](../.github/workflows/firmware-release.yml) はタグのMAJOR桁から基板バージョンを判定し（1でも2でもなければエラーで停止）、以下を実行する:

1. `config.h` の該当シリーズの `FIRMWARE_VERSION_BASE` を読み取り、タグの数字と一致するか検証する
   - **一致しない場合はここでエラー停止**（ビルドされない）。「config.hを更新し忘れたままタグを打った」ミスを検出する
2. `esp32-s3-devkitc-1-v{MAJOR}-release` env でビルド（`DEBUG_MODE` なし = `DEEP_SLEEP` モード）。`config.h` の値はCIが書き換えることはなく、コミットされている値がそのまま使われる
3. `firmware.bin` を `gzip -9` で圧縮して `firmware.bin.gz` を生成
4. `firmware.bin.gz` を S3 にアップロード（`firmware/vX.Y.Z.bin.gz`）
5. OTA Job ドキュメントを生成して S3 にアップロード（`jobs/vX.Y.Z.json`）
   - `url` フィールドは `.bin.gz` の S3 URL、`board_version` フィールドにMAJOR桁（1 or 2）を含む
6. `ota-target-car-iot-gw-v{MAJOR}` Thing グループに OTA ジョブを作成
7. GitHub Release を作成して該当基板の `firmware.bin` / `firmware.bin.gz` を添付

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
