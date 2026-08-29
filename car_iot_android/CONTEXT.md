# car_iot_android 引き継ぎメモ

## 概要

`mobile/`（Flutter版car-iotダッシュボード）を、Kotlin + Jetpack Compose単体の新規Androidネイティブ
アプリとして作り直しているプロジェクト。経緯・設計方針・全フェーズのロードマップは
`docs/car_iot_android_plan.md` を参照（このファイルは「今どこまで終わっていて、次に何をすべきか」の
実装ログ）。

**作業ブランチ**: `feat/car-iot-android`（mainには未マージ）。`mobile/`は当面残す（移植元参照用）。

## 現在の状態（Phase 0〜8完了、Phase 9より先行してテーマ/PiP実装済み）

| Phase | 内容 | 状態 |
|---|---|---|
| 0 | プロジェクト雛形 | 完了 |
| 1 | BLE接続＋OBDパーサー（TDD） | 完了 |
| 2 | 接続状態・計測値のUI表示 | 完了 |
| 3 | Foreground Service化 | 完了（**アプリを完全にタスクから消してもBLE接続・受信が継続することを実機確認済み**） |
| 4 | Cognito OAuth認証 | 完了（サインイン/アウト・セッション復元まで実機確認済み） |
| 5 | アップロード（Room永続化、dataSyncスロットリング） | 完了（実機確認済み、下記参照） |
| 6 | 位置情報 | 完了（実機確認済み、下記参照） |
| 7 | CDM連携（BLE検知でのkilled状態からの自動起動） | 完了（実機確認済み、下記参照） |
| 8 | 残りのUI（バッテリー/OBD/メータータブ、ゲージ4種） | 完了（エミュレータ確認済み、下記参照。**実データでのゲージ表示は次回実機接続時の宿題**） |
| - | テーマ切り替え＋PiP（Phase9より先行実装） | 完了（エミュレータ確認済み、下記参照。**PiPの実データ表示・実機での自動遷移は次回実機接続時の宿題**） |
| - | UIデザイン刷新（計器盤テーマ、Phase9より先行実装） | 完了（エミュレータ確認済み、下記参照） |
| 9 | 仕上げ | 未着手 |

次にやるならPhase 9（仕上げ: アイコン・署名設定等）から。設計は`docs/car_iot_android_plan.md`にまとめてある。

### UIデザイン刷新メモ（2026/08）

配色をレーシング/ミニマル → shadcn/ui(ライト/ダーク) → **計器盤ベースのナイト/デイ**と
3回作り直した。最終形と、なぜそこに至ったかを残す（同じ回り道を繰り返さないため）。

**採用しなかった2案とその理由**:

- **レーシング/ミニマル**: Material3デフォルトの`ColorScheme`だけ差し替えた版。
  ウィジェットは標準のままでピル型ボタン・太いProgressIndicatorが残り、
  「Material3そのまま」の印象を脱していなかった
- **shadcn/ui(neutral)**: Web管理画面のデザイン言語をそのまま持ち込んだ結果、
  グレー一色の事務的なUIになり、走行中に読む計器という被写体と噛み合わなかった

**現行（計器盤ベース）の設計判断**:

- **タイポグラフィが設計の核**（`ui/theme/ClusterTypography.kt`）。上記2案はいずれも
  ラベル11sp・数値22〜30spで差が小さく「数値も出る設定アプリ」に見えていた。
  実際の計器盤は極小ラベルと巨大数値の落差で出来ているため、
  ラベル10sp/字間+2.5sp ↔ ヒーロー数値60sp/字間-2sp まで開いた。数値は等幅かつ細字
  （桁が動かず、大きくしても圧迫感が出ない。太字は使わない）
- **アクセントは暖色アンバー**（`ui/theme/ClusterColorSchemes.kt`）。夜間の自動車メーター
  照明が実際にこの色域という被写体由来の語彙で、かつ「黒背景＋鮮やかな1色」という
  量産されがちな配色を避けられる。NIGHTの地色を純黒でなく青みのある`#06080C`に
  したのは、クラスターがガラス越しに冷たく沈んで見えることに倣ったもの
- **`ui/meter/ValueRail.kt`が署名要素**。Material3の`LinearProgressIndicator`/
  `CircularProgressIndicator`の使用をやめた。あれは「進捗」の表現で、計測値に進捗の
  概念は無い（電圧は0%から100%へ進行しない）うえ、太い塗り面が数値より目立つ。
  レンジ内の位置を1dpのヘアラインと2dpのティックだけで示す
- **バッテリータブはベントー構成**。均等2x2をやめメイン電圧を全幅ヒーローにし、
  運転中に見たいものをレイアウト自体で示す
- **無信号時は書式どおりの桁プレースホルダー**（`--.---`）。巨大な文字サイズでは
  `—`一文字が黒い帯に見えるのと、消灯セグメントを見せるのが計器の作法のため
- **ボタンの角丸は`ClusterButtonShape`を各所に明示指定**。Material3の`Button`は
  `Shapes`ではなく`CircleShape`固定なので、テーマ側の`Shapes`差し替えだけでは
  ピル型のまま変わらない

**Material 3 Expressiveについて**: Androidの現行デザイン言語（2025年5月発表、35種の
シェイプライブラリとスプリング系モーションが柱）だが、新コンポーネントAPIは
material3 1.4.0+が必要で本プロジェクトは1.3.2（compose-bom 2025.09.00）。依存追加は
確認事項のため今回は見送り、既存APIで組んでいる。将来BOMを上げる際に再検討の余地あり。

### テーマ切り替え＋PiP実機検証メモ（エミュレータ使用、BLE接続なし）

- テーマ切り替えは`FilterChip`での切り替え・両テーマの配色反映・
  アプリ再起動後の`SharedPreferences`永続化をエミュレータで確認済み
- PiP表示項目の選択ダイアログ（`ui/pip/PipSettingsDialog.kt`）は、初期選択
  （RPM/速度/水温）の表示・追加選択・保存・再起動後の永続化を確認済み
- PiPの自動遷移（`onUserLeaveHint()`）は**`ConnState.CONNECTED`時のみ**発火する設計だが、
  エミュレータはBLE接続できないためこの条件が満たせない。検証時は一時的にゲート条件を
  外した検証用ビルドでホームボタン操作→PiPウィンドウの表示（選択項目のラベル、
  値なし時のプレースホルダー）を確認し、確認後はゲート付きの本来のコードに戻して
  ビルド成功を再確認した。**次回実機接続時に、BLE接続中の自動PiP遷移と実データ表示を
  確認すること**

### 実機テスト結果（2026/08/28、Pixel 10a + fakeobd env）

エミュレータでは確認できなかった宿題を実機で全消化した。ESP32 は fakeobd ファームが
書き込まれた状態で USB 給電、スマホと BLE 直結という机上構成。

確認できたこと:

- BLE 接続（`car-iot-e072a1`）、ゲージ4種の実データ表示とライブ更新
- **PiP の自動遷移と実データ表示**（`ConnState.CONNECTED` ゲートがあるためエミュレータでは
  検証不能だった本命）
- **アプリをタスクから消しても BLE 受信と Room 蓄積が継続**（`pending_obd_reading` が
  1731→1736 件に増加することで確認）
- Cognito サインイン → アップロード（`OBDアップロード成功: 47件`）→ **S3 到達**
  （`obd/year=2026/month=08/day=28/hour=14/car-iot-e072a1-*.ndjson`）
- 保存レコードに全35項目＋`device_id`/`uploaded_by`(Cognito sub)/`ingest_ts`/`obd_ts`、
  さらに47件中46件に実 GPS 座標。1件だけ欠けるのは接続直後で `LocationTracker` の
  キャッシュが未充填のためで想定内

実機でしか踏めなかったバグ2件（いずれも修正済み）:

1. **PiP の最終行が見切れる** — `PictureInPictureParams` にアスペクト比を指定しておらず、
   システム既定の横長ウィンドウでは3項目が収まらなかった。項目数から比率を算出して渡す
2. **サインイン時に PiP がログイン画面を覆う** — `onUserLeaveHint()` はホーム/履歴キー
   だけでなく**アプリ自身が別 Activity を起動したときにも呼ばれる**。Custom Tabs を開く
   サインインでも PiP に入り、Google アカウント選択を妨げていた。`suppressPipOnLeave` で
   自分からの画面遷移時のみ抑止する

なお 2 の検証では「BLE 接続前にサインイン」してしまうと `CONNECTED` ゲートに掛からず
PiP が元から出ないため、**修正が効いたことの確認にならない**。未ログイン＋BLE 接続済み
という、壊れていたのと同じ条件を作って再現を試みること。

修正後に上記の条件（未ログイン＋BLE 接続済み＋ブラウザセッションなし）で再検証し、
サインイン操作から Cognito のログインページ表示まで **PiP が一度も出ないこと** を確認済み。
その状態でサインインを完了させ、アップロード（`OBDアップロード成功: 21件`、
`pending_obd_reading` 21→5 件）と S3 到達（`hour=15` パーティションに新規オブジェクト、
10 件すべてに GPS 座標と `uploaded_by` の Cognito sub）まで通しで確認した。
JST 0 時をまたいだため `day=28/hour=15`（UTC）に書かれるのが正しい挙動。

### 配色はWCAGのコントラスト比で検証する

視認性が要る要素は感覚で決めず、コントラスト比を計算して基準を満たすこと。走行中に
一瞥する画面なので、基準割れは見た目の問題ではなく読めない問題になる。

適用する基準（WCAG 2.1）:

| 対象 | 必要比 | 根拠 |
|---|---|---|
| 数値（大きい文字） | 3.0:1 | SC 1.4.3 大きいテキスト |
| ラベル10sp・本文・見出し・ボタン文字 | 4.5:1 | SC 1.4.3 通常テキスト |
| ValueRailの軌道・針・ティック・ボタン枠 | 3.0:1 | SC 1.4.11 非テキストコントラスト |

比較する背景は実際に乗る面を使う。Material3の`Card`は`surfaceContainerHighest`、
`OutlinedCard`と画面地は`surface`が容器色になるため、`background`と比べても意味がない。

この検証で分かった重要な点:

- **NIGHTのレール軌道は1.17:1でほぼ不可視だった**。ユーザーが気づいたのはDAY側
  （3.50:1）だが、実際にはNIGHT/GAUGEの方が深刻だった。暗色同士は破綻していても
  目視で気づきにくいので、数値で見ないと取りこぼす
- **アクセント色を小さな文字に使うのは脆い**。DAY/GAUGEでは、見出しが4.5:1を満たす
  明度とボタン文字（白）が4.5:1を満たす明度が両立しなかった。そのためアクセントは
  **図形（針・ティック・現在値）と塗りボタンに限定**し、見出し・PiPの値は前景色に
  変更した。実際の計器も区画名の刻印は白で、色が乗るのは針だけなので語彙としても合う

### 実機テストでやってはいけないこと

`pm clear` 等の**アプリ状態を変える adb コマンドは対象アプリにのみ実行する**。
Cognito のブラウザセッションだけ切るつもりで `adb shell pm clear com.android.chrome` を
実行し、ユーザーの Chrome のログイン状態・Cookie・初期設定を全消去してしまった失敗が
ある（結果、Google への完全再ログインが必要になりテストが中断した）。

認証セッションを切りたいだけなら、アプリ側のサインアウトや Cognito Hosted UI の logout
エンドポイントで足りる。詳細はルートの `CLAUDE.md`「adb 操作の鉄則」を参照。

### エミュレータでUIを確認する際のダミー値注入（有効だった手法）

BLE接続できない環境では計測値が常にnullで、プレースホルダー状態しか見られない。
デザインの良し悪しは実データが入った状態でないと判断できないため、以下を一時的に
書き換えて確認し、**確認後に必ず`git checkout --`で戻す**運用を取った。

- `state/CarIotState.kt`の`_measurement` / `_obdReading` / `_obdHistory`の初期値
  （バッテリー値・OBD35項目・スパークライン用の履歴）
- `meter/MeterConfigStore.kt`の`load()`冒頭で`defaultSlots()`を即returnさせる
  （保存済み設定に引きずられず、ゲージ4種すべてを一度に確認するため）

戻し忘れ防止として、注入する行には必ず`// TODO: デザイン確認用ダミー値(戻すこと)`を
付け、撤去後に`grep -rn "デザイン確認用" app/src/`と`git status`の両方で確認している。

### Phase 8後半実機検証メモ（エミュレータ使用、BLE接続なし）

このセッションは実機が無く、Android StudioのAVD（`Medium_Phone_API_36.1`、
`~/AppData/Local/Android/Sdk/emulator/emulator.exe -avd <name>`で起動）で検証した。
エミュレータは物理Bluetoothアダプタを持たずESP32とのBLE接続ができないため、
**実際のOBDデータでゲージ（特にsparklineの折れ線描画）が更新されることは未確認**。
確認できたのは: アプリがクラッシュせず起動する、`reading == null`時の空表示、
設定シートでの項目削除・スタイル変更・保存が画面に反映される、アプリ再起動後も
`SharedPreferences`の設定が保持される、JSON形式のエクスポート→インポートの往復
（SAF標準ファイルピッカー）が動作する、の5点。**次回実機接続時に、fakeobd env等で
OBDデータを受信させてゲージの実データ表示を確認すること**。

### Phase 8前半実機検証メモ

`MainActivity.kt`に`NavigationBar`で4タブ（接続/バッテリー/OBD/メーター）を導入し、
バッテリー・OBDタブを実装した（メータータブは「準備中」のプレースホルダーのまま）。

- fakeobd env接続状態で、バッテリータブ（vMain/curr/pwr/vSub、小数桁数・単位表示）と
  OBDタブ（35項目、`ObdMetric`のlabel/unit/decimalsに基づくフォーマット）の表示を
  実機で確認済み
- **レイアウト変更後は座標を取り直すこと**: `Scaffold`+`NavigationBar`導入でUIの
  座標が変わり、以前使っていた「接続」ボタン等の固定座標がずれて操作できなくなった。
  `adb shell uiautomator dump`で都度座標を確認すること
- タブアイコンには`androidx.compose.material:material-icons-extended`を使う
  （`-core`だけでは`Icons.Filled.Bluetooth`等の非基本アイコンが解決できない）

### Phase 7実機検証メモ

CDM連携は**minSdkを31→36に引き上げ、新API**（`ObservingDevicePresenceRequest`/
`onDevicePresenceEvent`）**のみ**で実装した（Android 16で旧API`onDeviceAppeared()`が
非推奨化されたため、詳細は`docs/car_iot_android_plan.md`のPhase7セクション参照）。

- **`ACCESS_BACKGROUND_LOCATION`が必須**: `CarIotForegroundService`はlocation型FGSのため、
  CDM経由（バックグラウンド）から起動すると、この権限が無いと**プロセスがクラッシュする**
  ことを実機で確認した。この権限を許可すればクラッシュしない（警告ログ自体は出るが動作する）
- **killed状態からの自動起動を実機確認済み**: `adb shell am force-stop`でプロセスを
  完全kill→ESP32のBLE再アドバタイズをCDMが検知→画面を一切開かずに`CarIotForegroundService`
  が起動し、BLE再接続・Notify購読・アップロードトリガーまで自動で完了することを確認した
- **検証時のハマりどころ**: CDMは一度Appeared通知済みのデバイスに対し、Disappearedが
  先に発生しない限り再度のAppeared通知をスキップする。Disappearedの発生には実際に
  デバイスが10秒前後見えなくなる必要があり、`force-stop`単体やBluetoothの
  disable/enableだけでは発生しないことがある。`adb shell dumpsys companiondevice`で
  `Nearby BLE Devices`の状態を見ながら検証するとよい
- **関連付けダイアログが透明に見えることがある**: 実際には裏で関連付けが完了している
  場合があり、`adb shell dumpsys companiondevice`の`Companion Device Associations`で
  確認できる。アプリの再インストールで古いタスクが残っていたことが原因の一つだった
  （端末再起動で解消）

### Phase 6実機検証メモ

`ACCESS_FINE_LOCATION`権限を「アプリ使用中のみ許可」で許可した状態でBLE接続し、
Room DBの`pending_obd_reading`テーブルに実際のGPS座標（緯度35.7651xx、経度139.6234xx付近、
検証場所の実際の現在地）が`lat`/`lon`列に正しく入ることを確認済み。

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
├── MainActivity.kt                 # NavigationBar(4タブ)の入れ物＋認証・BLE・CDM配線
├── AppConfig.kt(.example)          # terraform outputの値。実ファイルは.gitignore対象
├── ble/
│   ├── BleConstants.kt             # UUID定数
│   ├── ConnState.kt                # 接続状態enum + Measurement data class
│   ├── ObdChunkAssembler.kt        # チャンク再構成（ObdReadingへの依存なし、TDD済み）
│   └── BleConnectionManager.kt     # スキャン・接続・Notify購読、CarIotStateに直接書き込む
├── obd/
│   ├── Crc8.kt / ObdReading.kt     # TDD済み（テストはapp/src/test/kotlin/...）
│   └── ObdMetric.kt                # OBD全35項目のlabel/unit/decimals/min/max/valueOf、GaugeStyle・defaultMeterMetrics
├── meter/
│   ├── MeterSlot.kt                # メーター1タイル(項目+ゲージ種別)、MeterSlotJson(TDD済み)
│   ├── MeterConfigStore.kt         # SharedPreferencesへの設定永続化
│   └── PipConfigStore.kt           # PiP表示項目の永続化、PipMetricsJson(TDD済み)
├── ui/
│   ├── connection/ConnectionScreen.kt  # 接続タブ（サインイン/接続/自動起動/位置情報許可/テーマ切替/PiP設定）
│   ├── battery/BatteryScreen.kt        # バッテリータブ（ヒーロー1＋小タイル3のベントー構成）
│   ├── obd/ObdScreen.kt                # OBDタブ（35項目グリッド表示）
│   ├── theme/
│   │   ├── AppTheme.kt              # テーマenum(NIGHT/DAY)
│   │   ├── ClusterColorSchemes.kt   # 2テーマの配色定義（計器盤ベース、暖色アンバー）
│   │   ├── ClusterTypography.kt     # 極小ラベル↔巨大数値の落差を作る型（デザインの核）
│   │   ├── ClusterShapes.kt         # 角丸スケール＋ClusterButtonShape(ピル型回避)
│   │   └── ThemeStore.kt            # SharedPreferencesへのテーマ選択永続化
│   ├── pip/
│   │   ├── PipContent.kt            # PiPウィンドウ専用の簡易表示(ラベル+値のみ)
│   │   └── PipSettingsDialog.kt     # PiP表示項目の複数選択ダイアログ
│   └── meter/
│       ├── MeterScreen.kt          # メータータブ本体（2列グリッド、設定シート起動）
│       ├── GaugeWidgets.kt         # ゲージ4種(circular/digital/bar/sparkline)のComposable
│       ├── ValueRail.kt            # 署名要素。レンジ内の位置をヘアライン＋ティックで示す
│       └── MeterSettingsSheet.kt   # 項目追加/削除/スタイル変更＋JSON インポート/エクスポート
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
├── location/
│   └── LocationTracker.kt          # FusedLocationProviderClientで直近位置をキャッシュするだけの薄いクラス
├── service/
│   ├── CarIotForegroundService.kt  # connectedDevice|location型、BLE接続をActivityから独立させる
│   └── CarIotUploadService.kt      # dataSync型、起動のたびに未送信バッチを送信してstopSelf()
├── companion/
│   ├── CompanionDeviceHelper.kt    # CDM関連付け(associate)、新API(ObservingDevicePresenceRequest)
│   └── CarIotCompanionService.kt   # BLE検知(onDevicePresenceEvent)でCarIotForegroundServiceのみ起動
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
