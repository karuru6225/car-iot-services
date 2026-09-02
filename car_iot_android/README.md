# car_iot_android

ESP32-S3ゲートウェイ（`esp32_iot_gateway`）とBLEで通信するAndroidネイティブアプリ（Kotlin + Jetpack Compose）。
バッテリー電圧・OBD-IIデータの表示に加え、Foreground Service化によりアプリを閉じてもBLE受信・データ
アップロードを継続する。Flutter版`mobile/`の後継（移植元として当面`mobile/`は残す）。

## 特徴

- **アプリを閉じてもBLE接続・受信が継続**（`CarIotForegroundService`、connectedDevice|location型）
- **CDM(Companion Device Manager)連携**でESP32のBLEアドバタイズを検知し、killed状態からでも自動起動
- **Room永続化**によるアップロードキュー（`CarIotUploadService`、dataSync型、Service強制終了への耐性）
- Cognito（Google連携）認証、位置情報付きOBD-IIデータのAPI Gatewayアップロード
- 4タブ構成（接続/バッテリー/OBD/メーター）、メータータブはゲージ4種・設定のJSON入出力に対応
- **ピクチャーインピクチャー(PiP)** 表示、計器盤ベースのナイト/デイ2テーマ

設計判断の経緯・実機検証メモは[CONTEXT.md](CONTEXT.md)、UIデザインの指針は[DESIGN.md](DESIGN.md)、
全体アーキテクチャ・フェーズ分割の計画書は[docs/car_iot_android_plan.md](../docs/car_iot_android_plan.md)を参照。

## 必要環境

- Android Studio（JBR同梱、Gradle実行に使う）
- Android実機またはエミュレータ（**BLE機能の検証には実機が必須**。エミュレータは物理Bluetoothアダプタを持たない）
- `minSdk = 36`（CompanionDeviceManagerの新API`startObservingDevicePresence(ObservingDevicePresenceRequest)`がAPI36必須のため）

## セットアップ

### 1. `infra/`を先にterraform applyしておく

Cognito（`mobile/`と同じApp Clientを共用）・API Gatewayエンドポイント一式が必要。`infra/README`または
`infra/manage.ps1`を参照。GoogleフェデレーテッドログインのOAuthクライアント作成手順は
[mobile/README.md](../mobile/README.md)の該当セクションを参照（`mobile/`と設定を共有するため一度作れば両方で使える）。

### 2. `AppConfig.kt`を作成

`app/src/main/kotlin/info/karuru/cariot/AppConfig.kt.example`をコピーして`AppConfig.kt`を作成し、
`terraform output`の値を埋める（`AppConfig.kt`はgitignore対象、実値をコミットしない）。

```bash
cd infra && ./manage.ps1 output -Profile default
```

必要な値: `cognito_mobile_client_id` / `aws_region` / `cognito_user_pool_id` / `api_endpoint`

### 3. ビルド

```bash
cd car_iot_android
./gradlew.bat assembleDebug
```

Gradle実行にはJDK（javacコンパイラ込み）が必要。システムの`JAVA_HOME`がJREのみの場合は`gradle.properties`の
`org.gradle.java.home`でAndroid Studio同梱のJBRを指す設定済み。

### 4. ユニットテスト

```bash
./gradlew.bat testDebugUnitTest
```

BLE非依存の純粋ロジック層（CRC8、OBDパーサー、チャンク再構成、アップロードスロットリング判定等）はTDDで実装済み。

### 5. 実機で起動

実機を接続し、Android Studioから実行するか：

```bash
./gradlew.bat installDebug
```

## 実車がなくてもBLE〜アップロードまで検証する（fakeobd）

OBD-IIケーブルを車両に物理接続していない環境でも、ESP32側に固定値+ランダムなダミーOBDデータを
BLE Notifyで送出させる派生env（`esp32-s3-devkitc-1-v1-develop-fakeobd`）が用意されている。

```bash
cd esp32_iot_gateway
~/.platformio/penv/Scripts/pio.exe run -e esp32-s3-devkitc-1-v1-develop-fakeobd -t upload
```

BLE接続・ゲージ表示・PiP・バックグラウンド継続・アップロードは、この構成で実機検証済み
（詳細は[CONTEXT.md](CONTEXT.md)の実機テスト結果を参照）。**実車のOBD-IIポートに接続しての動作確認はまだ**。
本番ファームに戻す時は`esp32-s3-devkitc-1-v1-develop`envで書き込み直すこと。

## 開発方針

- Android API非依存の純粋ロジック層（`obd/`, `ble/ObdChunkAssembler.kt`, `upload/`のスロットリング判定等）は
  TDD（Red→Green→Refactor）で実装する
- UI(Compose)描画・BLE実機接続・Foreground Serviceのライフサイクルは自動テストでの検証が困難なため、
  実機での動作確認を主軸とする
