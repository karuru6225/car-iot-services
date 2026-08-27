# car-iot Android ネイティブアプリ 新規開発計画（Kotlin + Jetpack Compose）

既存のFlutterアプリ `mobile/`（ESP32-S3とBLE通信し、バッテリー電圧・OBD-IIデータを表示・AWSアップロード
するダッシュボード）を、**Kotlin + Jetpack Compose単体の新規Androidネイティブアプリとして作り直す**
計画。「アプリを閉じても裏でBLE接続・データ収集・アップロードを続けたい」という要望に応えるための
大規模なアーキテクチャ変更で、10フェーズ（Phase 0〜9）に分割して段階的に実装する。

---

## 背景・経緯

「Google Mapのナビのように、アプリを閉じても裏でデータ収集・アップロードを続けたい」という要望に対し、
Android的に保証された継続動作にはForeground Service化以外の方法がない。検証の結果、現在接続に使っている
`flutter_blue_plus`はbackground isolateでの動作に既知の未解決issue（[GitHub #683](https://github.com/boskokg/flutter_blue_plus/issues/683):
binary messenger初期化前のエラー）を抱えており、`flutter_background_service`のような別isolateベースの
仕組みと相性が悪いことが判明した。

議論の結果、UIも含めてFlutterを完全に手放し、**Kotlin + Jetpack Compose単体の新規アプリ**として作り直す
ことに決定した。「別アプリ間のIPC連携」のような複雑さを避けるため、最初から1つのアプリにUI・BLE・
バックグラウンド処理すべてを収める。既存の`mobile/`は当面残す（移植元の参照用、削除は未定）。

---

## 確定した設計方針

### プロジェクト基本情報

- **新規ディレクトリ**: リポジトリ直下`car_iot_android/`（`esp32_iot_gateway/`等と並ぶ独立プロジェクト）
- **パッケージ名/applicationId**: `info.karuru.cariot`（Flutter版`info.karuru.cariot_mobile`とは別。
  移行期間中に両アプリを実機に共存インストールして比較できるようにするため）
- **モジュール構成**: 単一`app`モジュールのみ。マルチモジュール化・DIコンテナ等は導入しない
- **minSdk = 31**（CompanionDeviceManagerの`startObservingDevicePresence`がAPI31必須）
- **重要な発見**: Cognitoのリダイレクトスキーム`info.karuru.cariotmobile://oauthredirect`は
  applicationIdと無関係な独立文字列（`infra/mobile_auth.tf`参照）なので、新アプリのManifestに同じ
  scheme/hostのintent-filterを持つActivityを用意すれば**Cognito側・Terraformの変更は一切不要**

### Foreground Serviceの設計（type分離）

型を偽らず正直に宣言する方針。「`dataSync`型の処理を`connectedDevice`型のService内に隠して6時間制限を
回避する」設計は、Google Play非公開でもOSの制約の趣旨を無視しており、`Service.onTimeout()`の技術的
リスクも残るため不採用とした。

- **`CarIotForegroundService`**（`connectedDevice|location`型）: BLE接続維持・OBD受信・位置情報取得。
  常時稼働、6時間上限なし
- **`CarIotUploadService`**（`dataSync`型）: AWSへのアップロードのみ。必要な時だけ
  `startForegroundService()`され、POST完了後すぐ`stopSelf()`する（常駐しない）

`dataSync`型は**Android 15（targetSdk 35）以降、24時間の累積で最大6時間**しか動けない（ローリング
ウィンドウ、ユーザーがアプリをフォアグラウンドに持ってきた時にリセットされる）。この上限に対する
安全策として、以下のスロットリングを設ける。

- 両Serviceとも直近24時間の稼働区間（開始・終了時刻のペア）を**Room**の`ServiceRuntimeSegment`
  テーブルに記録し、ローリングウィンドウ方式で累積稼働時間を計測する（SharedPreferencesでの
  JSON配列読み書きは書き込みのたびに全体を読み直す無駄があるため不採用、Roomの`SUM`/`DELETE`
  クエリで完結させる）
- `CarIotUploadService`は起動のたびに「自分の直近24時間累積稼働時間 ≥
  `CarIotForegroundService`の直近24時間累積稼働時間 × 0.25」を満たすなら今回のアップロードを
  見送り、バッファは保持したまま次回タイマーへ回す（見送り時はログに警告を残す）
- `onTimeout()`も実装し、万一閾値判定をすり抜けて6時間に到達した場合のフェイルセーフとする

**稼働時間の見積もり**: ESP32側は`onTick()`で1秒間隔にOBDデータをNotify送信しており
（`esp32_iot_gateway/src/service/mode_continuous_base.cpp`）、Flutter版`ObdUploader`のバッチサイズ上限
（100件）により実際には5分タイマーより先に約100秒毎にアップロードが発生する。24時間連続運転という
最悪ケースでの見積もりは合計稼働時間 約2.4時間（864回×10秒と仮定）で、6時間上限には収まるが余裕は
大きくない。そのため上記スロットリングを設ける。

**ネットワークタイムアウトは短めに設定する**: Flutter版`obd_uploader.dart`は
`.timeout(const Duration(seconds: 15))`だったが、上記の見積もり（1回あたり10秒と仮定）は電波不安定時に
タイムアウトいっぱいまで粘られると崩れる。OkHttpの`connectTimeout=5s`/`readTimeout=8s`/`writeTimeout=8s`
程度に短縮し、失敗は「次回のPendingObdReading再送」に任せて早めに`CarIotUploadService`を`stopSelf()`する。

### データ永続化（Room）

Flutter版`obd_uploader.dart`は「アプリが落ちたらバッファは失われる」設計だったが、バックグラウンド常駐を
目指す以上、Serviceがクラッシュ/OSに強制終了された場合の耐性をFlutter版より重視する。

- **`PendingObdReading`テーブル**: OBDの35フィールド＋`lat`/`lon`（nullable、位置情報未取得時はnull）
  ＋`ts`を1レコードとして保持。アップロード成功でDELETE、失敗時は残す。`_maxBufferHardCap`相当の件数
  上限は維持し、超過時は古いレコードから間引く
- **`ServiceRuntimeSegment`テーブル**: 上記の稼働時間ログ。同じRoom DBに同居させる（依存はどちらも
  Room 1つで済む）

OBDのミニグラフ表示（メーター画面のスパークライン、直近60件のリングバッファ）はこのRoom永続化とは別物。
表示専用・古いものは捨ててよいデータなので、`CarIotState`（後述）のメモリ上`StateFlow`にのみ保持する。
OBDデータを受信するのは`CarIotForegroundService`のみのため、リングバッファの更新もそちらが担当し、
`CarIotUploadService`は一切関与しない。

### CDM自動起動後の挙動

画面は前面に出さず、裏で`CarIotForegroundService`のみ起動する（`ContextCompat.startForegroundService()`、
`startActivity`は呼ばない）。運転中に画面が勝手に前面表示されるのを避けるため。

### Service⇄UI間のデータ受け渡し

Binderバインドは使わず、シングルトン`object CarIotState`が持つ`MutableStateFlow`群にServiceが書き込み、
UIは`asStateFlow()`で購読するだけ。UI→Serviceのコマンドは Intentのactionで送る
（`ContextCompat.startForegroundService(intent.setAction(ACTION_CONNECT))`等）。

### UIコンポーネント（ゲージ4種）

circular/digital/bar/sparklineの4種は**追加ライブラリ不要**。Material3の
`CircularProgressIndicator`/`LinearProgressIndicator`＋自作`Canvas`のsparklineで足りる。

### 新規ネイティブ依存

- `net.openid:appauth`（Cognito OAuth）
- `androidx.security:security-crypto`（トークン暗号化保存）
- `com.squareup.okhttp3:okhttp`（HTTP）
- `com.google.android.gms:play-services-location`（位置情報）
- `androidx.room`（OBDアップロードキュー・Service稼働ログの永続化）

他はAndroid標準APIで代替（BLEは`android.bluetooth.le`、メーター設定等の軽量な設定値はSharedPreferences）。

### 開発方針: テスト駆動開発（TDD）

Android API非依存の純粋ロジック層は、t-wada流のTDD（テストリストを先に作り、1ケースずつ
Red→Green→Refactorのサイクルを回す。仮実装→三角測量→明白な実装で一般化し、リファクタリングは
テストが通っている状態でのみ行う）で進める。

- **対象**: `obd/Crc8.kt`, `obd/ObdReading.kt`, `ble/ObdChunkAssembler.kt`, `upload/ObdUploader.kt`の
  バッチ・尾流し判定ロジック, `upload/UploadThrottle.kt`, `db/`のDAOクエリロジック（Robolectric or
  インメモリRoomでテスト可能）。特にPhase 1（BLE非依存のOBDパーサー群）とPhase 5（アップロード・
  スロットリング判定）はテストリストを書くところから着手する
- **対象外**: UI(Compose)の描画、BLE実機接続、Foreground Serviceのライフサイクル（実際のOS挙動、
  Doze/バッテリー最適化等）は自動テストでの検証が困難なため、引き続き実機での動作確認を主軸とする

---

## Kotlinパッケージ構成

```text
info/karuru/cariot/
├── CarIotApplication.kt              # 通知チャンネル作成
├── MainActivity.kt                   # ComponentActivity、Compose Nav、権限リクエスト起点
├── ble/        BleConstants.kt / ObdChunkAssembler.kt / BleConnectionManager.kt
├── obd/        ObdReading.kt / Crc8.kt / ObdMetric.kt
├── db/         CarIotDatabase.kt / PendingObdReading.kt(Entity+Dao) / ServiceRuntimeSegment.kt(Entity+Dao)
├── upload/     ObdUploader.kt / UploadThrottle.kt（累積稼働時間の閾値判定、db/のDaoを使う）
├── auth/       AuthTokenStore.kt / AuthLoginFlow.kt / OAuthRedirectActivity.kt
├── location/   LocationTracker.kt
├── companion/  CompanionDeviceHelper.kt / CarIotCompanionService.kt
├── service/    CarIotForegroundService.kt（connectedDevice|location） / CarIotUploadService.kt（dataSync）
├── state/      CarIotState.kt
├── meter/      MeterSlot.kt / MeterConfigStore.kt
└── ui/
    ├── theme/, CarIotViewModel.kt
    ├── connection/ (ConnectionScreen/AuthCard/ConnCard/AutoLaunchCard/DebugToggleCard/LogCard)
    ├── battery/ (BatteryScreen/MeasCard)
    ├── obd/ (ObdScreen/ObdCard)
    └── meter/ (MeterScreen/MeterTile/MeterSettingsSheet/GaugeWidgets)
```

命名はFlutter版`mobile/lib/`と1:1対応させ、移植時の突き合わせを容易にする。`ObdReading.kt`/`Crc8.kt`/
`ObdChunkAssembler.kt`はAndroid API非依存の純Kotlinで書き、`app/src/test/`にJUnitテストを置く
（エミュレータ不要で検証できる）。

---

## フェーズ分割（ロードマップ）

| # | フェーズ | 内容 | 検証 |
|---|---|---|---|
| **0** | プロジェクト雛形 | `car_iot_android/`作成、Compose依存のみの最小構成、空のCompose画面 | `./gradlew assembleDebug`が通り実機で画面が出る |
| 1 | BLE接続＋OBDパース（UIなし） | テストリスト作成→TDD（Red-Green-Refactor）で`Crc8`/`ObdReading`/`ObdChunkAssembler`を実装、`BleConstants`/`BleConnectionManager`、Logcat出力のみ | JUnitテスト全通過＋実機でESP32接続しLogcatに正しい値 |
| 2 | 最小限のUI（接続タブのみ） | Compose1画面、Activity内`lifecycleScope`で完結（Service化はまだ） | 実機で接続→数値表示→切断が動く |
| 3 | Foreground Service化 | `CarIotForegroundService`/`CarIotState`へ移設、通知・WakeLock | **アプリを完全にスワイプで閉じてもBLE接続・受信が継続**（要件の核心） |
| 4 | 認証（AppAuth） | `auth/`一式 | Cognito→Google→トークン取得→再起動後も復元 |
| 5 | アップロード | テストリスト作成→TDDで`ObdUploader`/`UploadThrottle`/DAOクエリを実装、`db/`（Room、PendingObdReading+ServiceRuntimeSegment）+ `CarIotUploadService`（dataSync型、別Service） | JUnitテスト全通過＋AWS側にデータ到達、バックグラウンド中も継続。Service強制終了後の再起動で未送信データが引き継がれること。累積稼働時間ログとスロットリング判定が機能すること |
| 6 | 位置情報 | `LocationTracker`組み込み | アップロードにlat/lon反映 |
| 7 | CDM連携 | `CompanionDeviceHelper`/`CarIotCompanionService`（Service起動のみ、画面は開かない） | 完全kill→ESP32起動→画面を触らず収集開始 |
| 8 | 残りのUI | バッテリー/OBD/メータータブ、ゲージ4種、設定インポート/エクスポート | Flutter版と同等の表示・操作 |
| 9 | 仕上げ | アイコン、署名設定、CLAUDE.md追記 | 最終回帰確認 |

各フェーズは独立してビルド確認・コミット可能（CLAUDE.mdの「1つの論理的変更→ビルド確認→コミット」方針）。

---

## Phase 0（初回実装範囲）

### 内容

- `car_iot_android/`をリポジトリ直下に新規作成
- Gradle Kotlin DSL構成（`settings.gradle.kts`, `build.gradle.kts`, `app/build.gradle.kts`）
  - 依存はComposeのみの最小構成（BLE/認証/位置情報等は後続フェーズで追加）
  - `applicationId = "info.karuru.cariot"`, `minSdk = 31`
- `app/src/main/AndroidManifest.xml`：最小構成（`MainActivity`のみ）
- `MainActivity.kt`：`ComponentActivity`、`setContent { }`でテキスト1つだけ表示するCompose画面
- アプリアイコン等は仮のデフォルトのままでよい（Phase 9で整える）

### 検証方法

1. 新規ディレクトリでGradle wrapperをセットアップし、`./gradlew assembleDebug`が通ることを確認
2. 実機（Android 12+）にインストールし、アプリが起動して画面が表示されることを確認

---

## Phase 5実装時の変更点（当初の設計方針からの差分）

Phase 5（アップロード機能）を実装する際、上記「確定した設計方針」の記述から以下の3点を
変更した。元の記述はそのまま残し、ここに差分と理由を追記する。

1. **ファイル名**: 上記では`upload/UploadThrottle.kt`としていたが、実装では
   `upload/RuntimeSegmentThrottle.kt`にした。「何の稼働時間を扱うスロットリングか」を
   ファイル名から読み取れるようにするため。
2. **稼働時間集計の実装方式**: 上記では「RoomのSUM/DELETEクエリで完結させる」としていたが、
   実装ではDAO（`ServiceRuntimeSegmentDao.segmentsOverlapping()`）は対象区間の一覧を
   返すだけにし、`RuntimeSegmentThrottle.overlappingRuntimeMs()`というKotlin側の純粋関数で
   集計する設計にした。ウィンドウ境界をまたぐ区間（`windowStart`より前から始まる区間、
   `endTs=null`で実行中の区間）を正確に按分するのはSQLのSUMだけでは表現しづらく、
   TDD可能な純粋関数に切り出した方が正確性とテスト容易性を両立できるため。
3. **尾流し判定の置き場所**: 上記では`ObdUploader`にバッチ・尾流し判定ロジックを
   持たせる想定だったが、実装では「OBDの`valid`がtrue→falseに変わった直後の即時
   アップロード」判定を`CarIotForegroundService`側に置いた。OBD受信の都度呼ばれるのは
   そちらのためで、`ObdUploader`はバッチ送信のみに責務を絞った。

---

## Phase 7: CDM連携（実装内容と当初想定からの差分）

ロードマップ表では「`CompanionDeviceHelper`/`CarIotCompanionService`」とだけ書かれていたが、
実装にあたり以下の点が新たに判明・確定した。

### CompanionDeviceManagerの新API採用とminSdk引き上げ

`android.jar`（compileSdk 36）を`javap`で確認したところ、Android 16（API 36）で
`CompanionDeviceManager.startObservingDevicePresence(String)`と
`CompanionDeviceService.onDeviceAppeared()`（Flutter版`feat/ble-companion-auto-launch`が
使っていた旧API）が非推奨化され、`startObservingDevicePresence(ObservingDevicePresenceRequest)`
+ `onDevicePresenceEvent(DevicePresenceEvent)`という新APIに置き換わっていることが分かった。
新APIは**API36必須**。ユーザーと相談の結果、個人利用アプリの規模には旧API対応の複雑さは
見合わないと判断し、**新APIのみ実装し`minSdk`を31→36に引き上げる**方針にした
（`app/build.gradle.kts`）。

新API利用時の実装ポイント:
- 関連付け成功時に得られる`AssociationInfo.id`（Int、MACアドレス文字列ではない）を
  `ObservingDevicePresenceRequest.Builder().setAssociationId(id).build()`に渡す
- `CompanionDeviceService`側は`onDevicePresenceEvent(event)`をオーバーライドし、
  `event.event == DevicePresenceEvent.EVENT_BLE_APPEARED`の時だけ処理する
  （`EVENT_BLE_DISAPPEARED`/`EVENT_BT_CONNECTED`/`EVENT_BT_DISCONNECTED`等もある）

### 実機で判明した重要な制約: ACCESS_BACKGROUND_LOCATIONが必須

`CarIotForegroundService`は`connectedDevice|location`型で宣言している（Phase6）。CDM経由
（バックグラウンド）から`startForegroundService()`でこれを起動すると、`ACCESS_BACKGROUND_LOCATION`
権限が無い場合はAndroidが「Foreground service started from background can not have
location/camera/microphone access」という制約を適用し、**プロセスがクラッシュする**ことが
実機検証で判明した（Android公式ドキュメントで裏取り済み: location型FGSをバックグラウンドから
起動するにはこの権限が必要）。

対応として`ACCESS_BACKGROUND_LOCATION`をManifestに追加し、`MainActivity`に許可導線
（`ACCESS_FINE_LOCATION`許可後に別途`ACCESS_BACKGROUND_LOCATION`をリクエストするボタン、
Android 11+の「常に許可」導線）を追加した。この権限さえあれば同じ警告ログは出るが
クラッシュはしない。

### CDMの「Appeared重複排除」の仕組みと実機検証の勘所

AOSPソース（`DevicePresenceProcessor.java`、`BleDeviceProcessor.java`）を確認したところ、
CDMは内部で「現在Appeared状態のassociationId集合」を保持しており、一度Appeared通知済みの
デバイスに対しては、再度`EVENT_BLE_APPEARED`が来ても**`EVENT_BLE_DISAPPEARED`が先に発生して
集合から除かれない限りコールバックしない**（重複通知防止のため）。

`EVENT_BLE_DISAPPEARED`はBLEスキャンの`MATCH_LOST`コールバックで発火し、実測ではデバイスが
実際に見えなくなってから10秒前後かかる。そのため実機で「killed状態からの自動起動」を
検証する際は:
- 単に`adb shell am force-stop`しただけではDisappearedが発生しないことがある
  （BluetoothGatt接続がシステム側にすぐには解放されないため）
- ESP32を物理的にリセットしても、起動が速すぎるとMATCH_LOSTのタイムアウトに届かず
  Disappearedが記録されないことがある
- 確実に検証したい場合は、`adb shell dumpsys companiondevice`で
  `Companion Device Present: Nearby BLE Devices`の状態を見て、対象デバイスが実際に
  消えているか確認するとよい

### 関連付けダイアログが透明に見える事象

`CompanionDeviceManager.associate()`のシステムダイアログ（`CompanionAssociationActivity`）が、
実機で透明な画面のまま操作不能に見えることがあった。実際には裏で関連付け処理は完了して
おり、`adb shell dumpsys companiondevice`で`Companion Device Associations`を確認すれば
association自体が作成されているかどうか判断できる（アプリの再インストールで残った
古いタスクスタックが原因だった可能性が高く、端末再起動で解消した）。
