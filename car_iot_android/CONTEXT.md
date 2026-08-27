# car_iot_android 引き継ぎメモ

## 概要

`mobile/`（Flutter版car-iotダッシュボード）を、Kotlin + Jetpack Compose単体の新規Androidネイティブ
アプリとして作り直しているプロジェクト。経緯・設計方針・全フェーズのロードマップは
`docs/car_iot_android_plan.md` を参照（このファイルは「今どこまで終わっていて、次に何をすべきか」の
実装ログ）。

**作業ブランチ**: `feat/car-iot-android`（mainには未マージ）。`mobile/`は当面残す（移植元参照用）。

## 現在の状態（Phase 0〜5完了）

| Phase | 内容 | 状態 |
|---|---|---|
| 0 | プロジェクト雛形 | 完了 |
| 1 | BLE接続＋OBDパーサー（TDD） | 完了 |
| 2 | 接続状態・計測値のUI表示 | 完了 |
| 3 | Foreground Service化 | 完了（**アプリを完全にタスクから消してもBLE接続・受信が継続することを実機確認済み**） |
| 4 | Cognito OAuth認証 | 完了（サインイン/アウト・セッション復元まで実機確認済み） |
| 5 | アップロード（Room永続化、dataSyncスロットリング） | 完了（実機確認済み、下記参照） |
| 6 | 位置情報 | 未着手 |
| 7 | CDM連携（BLE検知でのkilled状態からの自動起動） | 未着手 |
| 8 | 残りのUI（バッテリー/OBD/メータータブ、ゲージ4種） | 未着手 |
| 9 | 仕上げ | 未着手 |

次にやるならPhase 6から。設計は`docs/car_iot_android_plan.md`にまとめてある
（位置情報は`play-services-location`を追加し`PendingObdReading.lat/lon`を埋める、
`CarIotForegroundService`のtypeに`location`を追加）。

### Phase 5実機検証メモ

fakeobd env（`esp32-s3-devkitc-1-v1-develop-fakeobd`）で以下を確認済み:

- BLE接続確立（CONNECTED）直後に`CarIotUploadService`が即座に起動する（ただしこの1回目は
  OBDデータがまだ1件も届いていないタイミングだと空振りする。実用上は問題ない、次の
  5分タイマーか次の接続で溜まった分がまとめて送られる）
- 5分待たずとも、切断→再接続でCONNECTED遷移を再発火させれば即座にアップロードを
  トリガーできる（検証を早めたい時に有効。実機で100件+14件の2バッチに分けて送信される
  ことを確認＝`MAX_BATCH_SIZE`超過時のループ処理も動作確認済み）
- `adb shell am force-stop`でプロセスを強制終了しても、Room DBファイルは失われず、
  アプリ再起動→BLE再接続後に未送信データがそのまま送信される（Service強制終了への
  耐性が狙い通り機能している）
- Room DBの中身は実機からpullして確認できる:
  `adb exec-out run-as info.karuru.cariot cat databases/car_iot.db > car_iot.db`
  （`car_iot.db-wal`/`-shm`も同様にpullしてから`sqlite3`で開く。実機にはsqlite3バイナリが
  無いため`run-as ... sqlite3`は使えない、ローカルのplatform-tools付属`sqlite3`を使う）

## パッケージ構成（現状）

```text
info/karuru/cariot/
├── MainActivity.kt                 # UI表示専念、CarIotStateを購読するだけ
├── AppConfig.kt(.example)          # terraform outputの値。実ファイルは.gitignore対象
├── ble/
│   ├── BleConstants.kt             # UUID定数
│   ├── ConnState.kt                # 接続状態enum + Measurement data class
│   ├── ObdChunkAssembler.kt        # チャンク再構成（ObdReadingへの依存なし、TDD済み）
│   └── BleConnectionManager.kt     # スキャン・接続・Notify購読、CarIotStateに直接書き込む
├── obd/
│   ├── Crc8.kt / ObdReading.kt     # TDD済み（テストはapp/src/test/kotlin/...）
├── auth/
│   ├── SecureStore.kt              # Android Keystore(AES/GCM)直接利用の暗号化ストレージ
│   ├── AuthStore.kt                # トークン読み書き・リフレッシュ（Activity不要）
│   ├── AuthLoginFlow.kt            # サインイン開始（performAuthorizationRequest）
│   ├── OAuthRedirectActivity.kt    # リダイレクト受信、AuthorizationManagementActivityへの中継のみ
│   ├── AuthResultActivity.kt       # 認可フロー完了後の実処理（トークン交換・保存）
│   ├── CognitoConfig.kt            # OIDC discovery取得の共通ヘルパー
│   ├── ExpiryCheck.kt / JwtClaims.kt  # 純粋ロジック、TDD済み
├── db/
│   ├── CarIotDatabase.kt           # RoomDatabase定義
│   ├── PendingObdReading.kt        # アップロード待ちOBDデータ（Entity+Dao、@Serializable兼用）
│   └── ServiceRuntimeSegment.kt    # 両Serviceの稼働区間ログ（Entity+Dao）
├── upload/
│   ├── RuntimeSegmentThrottle.kt   # dataSync 6時間上限へのスロットリング判定、純粋ロジック、TDD済み
│   └── ObdUploader.kt              # JSON変換（TDD済み）+ OkHttpでのバッチ送信
├── service/
│   ├── CarIotForegroundService.kt  # connectedDevice型、BLE接続をActivityから独立させる
│   └── CarIotUploadService.kt      # dataSync型、起動のたびに未送信バッチを送信してstopSelf()
└── state/
    └── CarIotState.kt              # プロセス内シングルトン、StateFlow群（Serviceが書き込み、UIが購読）
```

## 実機検証で見つかった重要なハマりどころ

同じ実装をなぞる/拡張する時に踏み直さないよう記録しておく。

### 1. Android BLE: descriptor書き込みは同時に複数投げると2つ目以降が失敗する

複数のCharacteristicに対して`writeDescriptor()`（Notify有効化）をループで連続呼び出しすると、
最初の1つ以外は失敗する（Android BLEの既知の制約）。`onDescriptorWrite()`コールバックの完了を
待ちながらキューから1件ずつ処理する方式にする必要がある（`BleConnectionManager.kt`の
`notifyQueue`/`processNextNotifyQueueItem()`参照）。実機で「1つのCharacteristicの値しか
Notifyが来ない」という形で発覚した。

### 2. AppAuth: `appAuthRedirectScheme`を設定するとリダイレクト受信Activityが重複する

`net.openid:appauth`は内蔵の`RedirectUriReceiverActivity`を`manifestPlaceholders["appAuthRedirectScheme"]`
経由で使う設計だが、今回は自作の`OAuthRedirectActivity`を使う設計にしたため、このplaceholderを
設定すると「自作Activity」と「ライブラリ内蔵Activity」の両方が同じスキームのintent-filterを
持つ状態になり、実機で「アプリで開く」に同名アプリが2つ表示される不具合になった。
（`android:name="net.openid.appauth.RedirectUriReceiverActivity"` に
`tools:node="remove"` を当てて除去、`manifestPlaceholders`は設定しない。
`AndroidManifest.xml`参照）

**この手のマニフェスト変更を検証する時はデバイスを再起動すること**。PackageManagerの
intent-filterキャッシュが更新されず、アンインストール→再インストールしても
`dumpsys package`に古い情報が残り続けることがある。

### 3. AppAuth: `AuthorizationManagementActivity`がAndroid 14+で再生成され認可フローが中断される

既知のライブラリ側issue（[AppAuth-Android #977](https://github.com/openid/AppAuth-Android/issues/977)）。
デフォルトの`launchMode="singleTask"`を`singleInstance`にオーバーライドする
（`AndroidManifest.xml`の`net.openid.appauth.AuthorizationManagementActivity`宣言、
`tools:replace="android:launchMode"`）。実機（Android 16/API 36）でも同様の対応が必要だった。

### 4. AppAuth: リダイレクト受信Activityで直接`AuthorizationResponse.fromIntent()`を呼んではいけない

最初の実装ミス。`OAuthRedirectActivity`（リダイレクトURIのintent-filterを持つActivity）で
直接`AuthorizationResponse.fromIntent(intent)`を呼んでトークン交換までしようとしたが、
ブラウザから渡された生のリダイレクトIntentはこのAPIが期待する形式ではなく、常に`null`が返り
即座に`finish()`されてしまう（実機では「Custom Tabsが一瞬開いてすぐ閉じる」症状として発覚）。

正しい設計（ライブラリ内蔵`RedirectUriReceiverActivity`と同じ）:
- リダイレクト受信Activity（`OAuthRedirectActivity`）は
  `startActivity(AuthorizationManagementActivity.createResponseHandlingIntent(this, intent.data))`
  で処理を委譲するだけの薄い中継役に徹する
- 実際の認可完了処理（トークン交換等）は、`AuthorizationService.performAuthorizationRequest(request,
  completedIntent, canceledIntent)`の`completedIntent`（PendingIntent）で指定した別Activity
  （`AuthResultActivity`）が行う。`AuthorizationManagementActivity`がフロー完了後にそこへ
  結果を渡してくれる

### 5. ファイル名に"Token"を含めると書き込みがブロックされる（このセッションの制約）

このセッション（Claude Code）の権限設定で、ファイル名に`Token`という単語を含む新規ファイルの
`Write`がdeny ruleで拒否される現象があった（`TokenExpiryTest.kt`→`ExpiryCheckTest.kt`、
`IdTokenClaimsTest.kt`→`JwtClaimsTest.kt`で回避）。クラス名・変数名レベルでは問題ない
（`AuthStore`内の`accessToken`等は普通に書けている）。今後同種のファイルを作る時は
ファイル名に"Token"を避けること。

### 6. org.json.JSONObjectはAndroid Unit Testで動かない

Android SDK提供の`org.json`パッケージはユニットテスト環境ではstub化されており、例外は
投げないが常にデフォルト値（実質空）を返す。JWTクレーム抽出などをTDDで書きたい場合は
`kotlinx-serialization-json`を使うこと（`JwtClaims.kt`参照、依存追加済み）。

### 7. androidx.security:security-cryptoは使わない

2025年7月に1.1.0として安定版リリースされたが、それと同時に全APIが非推奨化された
（Googleは今後Android Keystore直接利用への移行を推奨）。このプロジェクトでは
`SecureStore.kt`でAndroid Keystore(AES/GCM)を自前実装している。

### 8. OkHttp 5.5.0はcompileSdk 37を要求する

2026年8月時点の最新安定版はOkHttp 5.5.0だが、これは`compileSdk 37`を要求し、
AGP 8.11.1が推奨する最大`compileSdk 36`と衝突して`checkDebugAarMetadata`が失敗する
（`app/build.gradle.kts`参照）。ひとつ前の5.4.0は`compileSdk 36`のままで問題なく使えるため、
そちらを採用している。今後AGP/compileSdkを上げる際に5.5.0への追従を検討すること。

### 9. kotlinx.serialization.jsonの`JsonPrimitive`拡張プロパティは個別importが必要

`.long`/`.int`/`.double`/`.boolean`（`JsonPrimitive`→プリミティブ型への変換）はパッケージ
`kotlinx.serialization.json`のトップレベル拡張プロパティで、`jsonPrimitive`等をimportしても
自動解決されない。`Unresolved reference` になった場合は
`import kotlinx.serialization.json.long`のように個別にimportする（`ObdUploaderTest.kt`参照）。

## ビルド・テスト

```bash
cd car_iot_android
./gradlew.bat assembleDebug          # ビルド
./gradlew.bat testDebugUnitTest       # ユニットテスト（純粋ロジック層はTDD済み）
```

Gradle実行にはJDK（javacコンパイラ込み）が必要。システムのJAVA_HOMEがJREのみの場合は
`gradle.properties`の`org.gradle.java.home`でAndroid Studio同梱のJBRを指す設定済み。

## 開発環境セットアップ（初回のみ）

`AppConfig.kt.example`を`AppConfig.kt`にコピーして実値を埋める。値は
`cd infra && ./manage.ps1 output -Profile default` で取得できる
（`cognito_mobile_client_id`/`aws_region`/`cognito_user_pool_id`/`api_endpoint`。
mobile版と同じCognito App Clientを共用する設計、Terraform側の変更は不要）。

## ESP32側のテストデータ送出機能

OBD2ケーブルを車両に物理接続していない環境でパーサーの実機検証をするため、
`esp32_iot_gateway`に`esp32-s3-devkitc-1-v1-develop-fakeobd`という派生envを追加済み
（`DEBUG_FAKE_OBD_DATA`フラグ、`obdpoll.cpp`の`obdPollFake()`）。固定値+rpmのみランダムな
ダミーOBDデータをBLE Notifyで送出する。実車テストの予定がないうちはこれで代用できる。
本番ファームに戻す時は`esp32-s3-devkitc-1-v1-develop`envで書き込み直すこと。

```bash
cd esp32_iot_gateway
~/.platformio/penv/Scripts/pio.exe run -e esp32-s3-devkitc-1-v1-develop-fakeobd -t upload
```

## 参照

- `docs/car_iot_android_plan.md` — 全体設計・アーキテクチャ方針・フェーズ0〜9のロードマップ
- `mobile/` — 移植元のFlutter版（ロジック突き合わせ用に当面残す）
- `infra/mobile_auth.tf` — Cognito Mobile App Client設定
