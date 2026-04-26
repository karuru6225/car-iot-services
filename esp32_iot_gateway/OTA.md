# OTA 仕様

## 概要

AWS IoT Jobs 経由で OTA 更新ジョブを受け取り、HTTPS でファームウェアをダウンロード・書き込みする。
ロールバック機能により、不正なファームの自動巻き戻しをサポートする。

---

## フラッシュパーティション構成

```
0x0000  Bootloader
0x8000  パーティションテーブル（4KB）
0x9000  nvs       20KB  設定・証明書アップロード済みフラグ
0xe000  otadata    8KB  OTA 起動スロット管理（Bootloader が参照）
0x10000 app0    3840KB  OTA スロット 0
0x3D000 app1    3840KB  OTA スロット 1
0x79000 spiffs   448KB  ファイルシステム（将来用）
```

app0 / app1 を交互に使用する 2 スロット方式。factory パーティションなし。

---

## 起動フロー

```
電源 ON / DeepSleep 復帰
        ↓
Bootloader: otadata を読み、起動スロット（app0 or app1）を決定
        ↓
アプリ起動
        ↓
lte.setup()   モデム初期化 → 証明書アップロード（初回のみ） → LTE 接続 → 時刻同期
        ↓
ota.check()   Jobs $next/get → ジョブドキュメント取得
        ├── 更新あり → apply() → esp_restart()  ※戻らない
        └── 更新なし / 失敗 → 次へ
        ↓
ota.confirmBoot()   PENDING_VERIFY 状態なら起動確認を確定
        ↓
DeepSleep（300秒）
```

---

## OTA 更新フロー

### AWS IoT Jobs の役割

AWS IoT Jobs は OTA 更新指示をキューとして管理する。デバイスがオフライン中でもジョブは保持され、次回起動時に確実に配信される。

```
AWS 側でジョブ作成
    ↓（デバイスがオフラインでも保持）
デバイス起動 → $next/get で問い合わせ → ジョブドキュメント受信
    ↓
OTA 適用
    ↓
ジョブ完了ステータスを更新
```

### ジョブドキュメント形式

```json
{
  "operation": "ota",
  "version": "1.2.0",
  "url": "https://your-bucket.s3.ap-northeast-1.amazonaws.com/firmware/v1.2.0.bin"
}
```

### デバイス側の MQTT トピック

| 操作 | トピック | 方向 |
| ------ | --------- | ------ |
| 次のジョブを取得 | `$aws/things/{thingName}/jobs/$next/get` | publish |
| ジョブドキュメント受信 | `$aws/things/{thingName}/jobs/$next/get/accepted` | subscribe |
| ジョブ状態を更新 | `$aws/things/{thingName}/jobs/{jobId}/update` | publish |
| 新ジョブの通知 | `$aws/things/{thingName}/jobs/notify-next` | subscribe |

### ジョブ状態の遷移

```
QUEUED（AWS側でジョブ作成）
    ↓ デバイスが $next/get で取得
IN_PROGRESS（更新に publish）
    ↓
    ├── apply() 成功 → SUCCEEDED（再起動後に更新）
    └── 失敗       → FAILED（理由を jobDocument に付記）
```

### 書き込み手順

1. `esp_ota_get_next_update_partition()` で書き込み先スロットを決定（現在と逆のスロット）
2. Jobs ジョブ状態を IN_PROGRESS に更新
3. `esp_ota_begin()` で書き込み開始
4. HTTPS GET でファームウェアを 512 バイト単位でストリーミング DL し `esp_ota_write()` に渡す
5. `esp_ota_end()` で検証（SHA256 チェック）
6. `esp_ota_set_boot_partition()` で次回起動先を新スロットに設定
7. `esp_restart()` で再起動（再起動後に SUCCEEDED を更新）

### ダウンロード

- HTTPS（TLS）で直接ダウンロード。MQTT 接続とは独立した TLS セッション
- S3 署名付き URL 対応（URL 最大 768 バイト）
- client cert 不要（サーバー証明書のみで検証）

### AWS 側の操作手順

```bash
# ジョブドキュメントを S3 にアップロード
aws s3 cp job.json s3://your-bucket/jobs/v1.2.0.json

# ファームバイナリをアップロード
aws s3 cp firmware.bin s3://your-bucket/firmware/v1.2.0.bin

# ジョブを作成
aws iot create-job \
  --job-id "ota-v1.2.0" \
  --targets "arn:aws:iot:ap-northeast-1:{accountId}:thing/{thingName}" \
  --document-source "https://your-bucket.s3.ap-northeast-1.amazonaws.com/jobs/v1.2.0.json"
```

---

## ロールバック機能

`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`（`sdkconfig.defaults` で有効化）

### パーティション状態遷移

```
OTA 書き込み完了
        ↓ esp_ota_set_boot_partition()
NEW（新ファーム、初回起動監視中）
        ↓ esp_restart() → Bootloader
PENDING_VERIFY（確認待ち）
        ↓
    ┌── esp_ota_mark_app_valid_cancel_rollback() が呼ばれた
    │       ↓
    │   VALID（確定）→ Jobs に SUCCEEDED を通知
    │
    └── 確認されないまま再起動
            ↓
        ABORTED → 旧スロットで起動（ロールバック）→ Jobs に FAILED を通知
```

### confirmBoot() の呼び出しタイミング

`main.cpp` で `ota.check()` の直後に `lte.isMqttConnected()` を条件として呼ぶ。
MQTT 接続が確認できた = 証明書・ネットワークともに正常と判断する。

- Jobs 経由で SUCCEEDED を報告済みの場合（`reportPendingJobResult()` が処理済み）は `confirmBoot()` は no-op になる
- LTE/MQTT 障害時は `confirmBoot()` がスキップされ、次回起動でロールバックされる

---

## 証明書・デバイス設定管理

### 保存場所

ビルド時にファームバイナリへ埋め込む秘密情報はない。すべてプロビジョニング時に書き込む。

| データ | 保存場所 | 内容 |
| ------ | -------- | ---- |
| CA cert | SPIFFS `/certs/ca.crt` | Amazon Root CA |
| device cert | SPIFFS `/certs/device.crt` | デバイス証明書 |
| device key | SPIFFS `/certs/device.key` | デバイス秘密鍵 |
| MQTT ホスト | NVS `device/mqtt_host` | IoT Core エンドポイント |
| device ID | 不要（MAC アドレスから起動時に生成） | `esp32-gw-aabbccddeeff` 形式 |

### SIM7080G へのアップロード判断

SPIFFS 上の証明書ファイル3本の CRC32 を NVS（`lte/cert_crc`）に保存済みの値と比較する。

- **一致**: スキップ（SIM7080G フラッシュへの書き込みなし）
- **不一致 / 未保存**: アップロードして CRC を更新

### 証明書の更新手順

1. AWS IoT Core で新しい証明書を発行し、同じ Thing にアタッチ（旧証明書は有効のまま残す）
2. プロビジョニングスクリプトで新しい cert を SPIFFS に書き込む（`pio run -t uploadfs`）
3. 再起動時に CRC 不一致を検出 → SIM7080G に新 cert をアップロード
4. 動作確認後、AWS IoT Core で旧証明書を無効化

### 注意事項

- 証明書が無効な状態では MQTT 接続できないため OTA トリガーを受け取れない
- 完全に詰まった場合は USB 直結での書き込みで復旧する
- AWS IoT Core の device cert はデフォルトで有効期限なし

---

## ロールバックで対応できるケース

| ケース | 動作 |
|--------|------|
| 起動直後にクラッシュ | `confirmBoot()` 未到達 → 次回起動でロールバック |
| LTE 接続不能 | `isMqttConnected()` が false → `confirmBoot()` スキップ → ロールバック |
| 悪い cert を OTA 配信 | MQTT 接続失敗 → `isMqttConnected()` が false → ロールバック |

---

## 実装状況

| 機能 | 状態 |
| ------ | ------ |
| デュアルバンク OTA（書き込み・再起動） | 実装済み |
| ロールバック（CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE） | 実装済み |
| confirmBoot()（isMqttConnected() 条件付き） | 実装済み |
| 証明書 CRC 管理 | 実装済み |
| AWS IoT Jobs 対応 | 実装済み |
| firmware.bin QIO パッチ（extra_scripts + esptool） | 実装済み |
| AWS IAM Policy（Jobs トピックの publish/subscribe 権限） | **未対応** |
| reportPendingJobResult() の MQTT 接続確認 | **未対応**（Jobs publish 前に接続を確認していない） |

---

## 制約・前提

- OTA バイナリの最大サイズ: **3840KB**（各スロットのサイズ）
- factory パーティションなし。完全故障時は物理アクセスで復旧
- ファームバイナリに秘密情報なし。証明書・デバイス設定はプロビジョニング時に書き込む
- 共通ファームの OTA 配信が可能（device ID は MAC から生成、cert/設定は SPIFFS/NVS から読む）
