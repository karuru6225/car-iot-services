# 引き継ぎ: ISO-TP マルチフレーム対応と多PID要求の実装

対象リポジトリ: `karuru6225/car-iot-services` / `esp32_iot_gateway`
背景: VEマップ作成のための高レート(10Hz級)OBDポーリング「ダイノモード」構想の前段として、
CAN層の制約を解消する。本書はチャット側での調査結果と実装方針の引き継ぎ。

---

## 0. 進捗状況（別PCでの継続用）

**ブランチ**: `isotp-multipid`（[#6](https://github.com/karuru6225/car-iot-services/pull/6)として`main`へマージ済み）
**リリース**: `v1.21.0`としてOTAリリース済み（v1本番車両、2026-08-07）。v2系は開発中のため対象外

### 完了（コミット済み・ビルド確認済み。実車確認状況は各項目末尾に記載）

1. **リファクタ（タスク1着手前の下地整備）**
   - `obd.cpp` の `checkHeader()` をペイロード先頭ポインタ返却方式に変更し、全28
     `obdDecode*()` 関数を `data[3+N]` → `p[N]` 参照に更新。PCIバイトは `can.cpp` 側で
     剥がして返す契約に統一（SF/マルチフレームでインデックスがズレる問題を解消）。
     union/reinterpret_castによる構造体オーバーレイは、OBDのビッグエンディアン値と
     ESP32のリトルエンディアンの不整合・strict-aliasing違反のリスクがあるため不採用。
   - `can.cpp` のID一致判定（29bit/11bitフォールバック）を `isObdResponseFrame()` に抽出。
2. **タスク1: ISO-TP受信の最小実装**（`canReceiveObdResponse()` を拡張、新関数は作らず）
   - SF/FF/CFをPCIバイト（`data[0]&0xF0`）で分岐。SF長は `dlc-1` 固定ではなく
     PCI宣言長（`data[0]&0x0F`）から取得するよう修正（ハンドオフ§3タスク1準拠）。
   - FF受信直後にFCを自動送信（`sendFlowControl()`、宛先IDは受信IDの下位バイトから導出、
     ハードコードなし）。CF組み立ては `receiveConsecutiveFrames()`、CF間タイムアウト50ms。
   - `maxLen` 引数を追加（デフォルト8＝既存呼び出し元と同じ、回帰なし）。応答長が
     `maxLen` を超える場合は安全側に倒して失敗を返す。
   - 11bitフォールバック経路でのマルチフレームは非対応（実質未使用のため対象外）。
3. **タスク4の下準備**（本実装ではなく実車確認のための布石）
   - `obdpoll.cpp` の `kPids` に `0x68` を追加。バイト割り当てが実測未確定なため、
     `obd.cpp` には正式デコーダを実装せず、`obdpoll.cpp` 内の一時関数
     `decodeChargeAirTempRaw()` でヘッダ一致のみ確認して生データをログ出力する
     （`OBDReading` へは反映しない。常に `decodeFailCount` にカウントされるのは意図通り）。
   - 受信バッファを `data[8]` → `data[64]` に拡張し、`canReceiveObdResponse()` の
     `maxLen` を明示的に渡すよう変更（0x68の応答9バイトはSF上限7バイトを超えるため）。
4. **タスク4本編**: 実車で `41 68 03 4A 49 00 00 00 00`（Sensor1=34°C, Sensor2=33°C）を確認。
   Sensor1/2とインタークーラー前後の物理対応は未確定のまま(保留)、`decodeChargeAirTempRaw`を
   削除して`obd.cpp`に`obdDecodeChargeAirTemp()`を実装。`OBDReading`/`ObdBlePacket`（末尾に
   `iat_c`/`iat2_c`を追加、既存フィールドのオフセットは不変）/mobile側
   （`obd_reading.dart`/`obd_metric.dart`/`obd_uploader.dart`）/Lambda ingest/Athenaスキーマへ
   反映済み。
5. **§4 テスト1（原因A/B切り分け）**: 診断用一時関数`canSendObdRequestTest2Pid()`
   （`can.h/.cpp`）を追加し、RPM(0x0C)+MAP(0x0B)を1フレームにまとめて要求、
   `obdpoll.cpp`から毎サイクル`[TEST1]`タグで生応答をログ出力。実車で
   `41 0C 0E 3C 0B 2A`のように**両PIDとも返る**ことを複数サンプルで確認。
   → **原因B確定**（ECUは多PID要求に対応している。過去の「1個だけ返る」不具合は
   受信側の実装不備が原因だった）。**タスク2・タスク3を実施する価値あり**。
   結論が出たので診断用コードは削除済み（タスク2の正式実装で置き換え）。
6. **タスク2: 多PID要求の送信対応**（`can.h/.cpp`に`canSendObdRequestMulti()`を追加）。
   `data[0]=1+N`(PCI: SF, N=PID数, 最大6)、`data[1]=0x01`、`data[2..]=PIDs`で
   1フレーム送信。**物理アドレッシングは未実装**（§4テスト1でfunctional
   `0x18DB33F1`のまま2PID要求が問題なく通ることを実車確認済みのため、ECUアドレス
   学習ロジックを追加する必要性が今のところ無いと判断・見送り）。
   `obdpoll.cpp`からはまだ呼ばれていない（呼び出し元はタスク3で追加予定）。
7. **タスク3: 多PID応答パーサ**（`obd.h/.cpp`に`obdParseMultiResponse()`を追加）。
   PID→データ長テーブル`kPidLengths[]`（現行29PID分、各`obdDecode*()`の
   `checkHeader()` minDlcから算出）を持ち、`41 [PID][data...] [PID][data...] ...`を
   コールバック形式（`ObdMultiSegmentCb`）でPIDセグメント単位に分解する。
   未知PID遭遇時は境界が不明なため打ち切る設計（欠落は自然に許容、テーブル外は打ち切り）。
   実車確認用に`obdpoll.cpp`へ最小の診断配線を追加（`obdPoll()`末尾、通常の29PID集計
   とは独立）: `canSendObdRequestMulti({0x0C,0x0B})` → `obdParseMultiResponse()` →
   `[TEST2]`タグでセグメントをログ出力。実車で`pid=0x0C len=2 data=0E EC`
   （RPM=955、同時刻の通常ポーリング結果と一致）・`pid=0x0B len=1 data=28`
   （MAP=40kPa、同上）を複数サンプルで確認。**送信API・パーサとも実車動作確認完了**。
8. **本番統合**: `obdpoll.cpp`の`kPids`ループを書き換え、診断用コード（`[TEST2]`まわり）
   は削除。29PIDを`kMaxPidsPerRequest`=6ずつのグループ（計5リクエスト）に分割し、
   グループごとに`canSendObdRequestMulti()`→`canReceiveObdResponse()`→
   `obdParseMultiResponse()`で処理。各セグメントは一時バッファに`41 [pid] [data...]`
   形式で詰め直し、既存の`obdDecode*()`をそのまま呼び出す（デコーダ本体は無変更）。
   ログに`未応答PID`カウンタを追加（グループ受信は成功したが特定PIDのセグメントが
   応答に含まれなかったケースを、送信失敗・応答なし・デコード失敗と区別して計上）。
   異常時最悪サイクル時間は29×50ms=1.45秒→5×50ms=250ms程度に短縮。
9. **バッチ化バグ修正**: 実車で`coolant=0C`（stft/ltftも0.0%固定）・
   `未応答PID=3`（0x67/0x06/0x07が欠落）を確認。原因は`kPidLengths[]`の
   `0x66`/`0x67`/`0x68`（マスクバイト+複数センサー枠を持つ拡張PID群）を
   `checkHeader()`のminDlcから機械的に算出していたため、デコーダが実際には
   読まない後続センサー分のバイトを勘定に入れておらず、応答内の次PIDの
   位置を見失っていたこと。一時的に`obdpoll.cpp`へ生応答ダンプ
   （`[OBD-RAW] group=N dlc=... : <hex>`）を追加して実車で採取し、手計算で
   実際のセグメント長を特定（`0x66`: 3→**5**バイト＝マスク+2センサー×2byte
   [未対応センサー分は0埋め]、`0x67`: 2→**3**バイト＝マスク+2センサー×1byte、
   `0x68`: 3→**7**バイト＝マスク+6byte、旧単発応答dlc=9と整合）。
   5グループ全てで応答バイトを過不足なく消費できることを検算で確認してから
   `kPidLengths[]`とバッファサイズ`kMaxSegmentDataLen`（4→7）を修正、診断
   ダンプは削除。実車で`[OBD] poll: OK=29/29 ... 未応答PID=0`、
   `coolant=91C`、`stft`/`ltft`とも変動値に復帰したことを確認。
   **タスク2・タスク3・本番統合、全て実車動作確認完了**。

### 現状まとめ

本ドキュメントで計画したISO-TPマルチフレーム対応・0x68正式デコーダ・多PID
送信/パーサ・本番バッチ統合は、実車確認まで含めて完了。PR#6を`main`へ
マージ済み、`v1.21.0`としてOTAリリース済み（v1本番車両）。AWS上のデータ
（Athena `obd_data`テーブル）でも修正前後の切り替わりと値の妥当性を確認済み
（修正前は`coolant_c=0`等の汚染データが残っているが、見た目で識別可能なため
削除せず放置する方針）。

### 残作業（任意）

- センサー1/2とインタークーラー前後の物理対応の実測確定（`iat_c`/`iat2_c`、
  過給時のログが必要）。
- ダイノモード（TPS>90%等トリガーの10Hz級リングバッファ録画、§5参照）は
  本ドキュメントのスコープ外のまま、着手する場合は別途計画する。
- **実走行（速度・RPM・負荷が変動する状態）およびIGN OFF/CAN未接続時の
  異常系パスは未検証のままリリース済み**。アイドル・停車・IGN ONの範囲でのみ
  実車確認している。ロジック上は問題ないはずだが（`sendFailCount`/
  `recvFailCount`はグループ単位で従来と同じ分岐に落ちる設計）、実走行で
  違和感があれば真っ先にここを疑うこと。
- 単発PID版`canSendObdRequest(uint8_t pid)`（`can.h/.cpp`）が、`obdpoll.cpp`を
  バッチ化した結果どこからも呼ばれなくなり未使用のまま残っている。将来
  デバッグ用途（メニューから単発PIDを叩く等）に使う予定が無ければ削除対象。

---

## 1. 背景と経緯

- Grafana/Athena上のOBDログ(1Hz)からMAF/MAP/RPMでVE(体積効率)マップを試算済み。
  MAF由来VEとabsolute_load_pct(PID 0x43)由来VEの相関0.935で手法は妥当と確認。
- 過渡(加速)中のVE外れ値はインマニ充填動態が原因。10Hz級で dMAP/dt を取れば
  `m_engine = m_MAF - (V_man/RT)·dP/dt` で補正可能 → 高レート化したい。
- 高レート化には1リクエストで複数PIDをまとめたいが、**過去に多PID要求を実験して失敗**
  (「1個だけデータが返ってきた」)。今回その原因をコードレベルで特定した。

## 2. 診断結果(チャット側調査)

### 確定: ISO-TP(ISO 15765-2)のマルチフレーム受信が未実装

- `src/device/can.cpp:116` `canSendObdRequest()` — Single Frame固定 (`data[0]=0x02`)。
- `src/device/can.cpp:153` `canReceiveObdResponse()` — ID一致した**最初の1フレームを
  memcpyして即return**。Flow Control送信・Consecutive Frame組み立てはリポジトリ内に存在しない。
- 多PID応答はペイロード7バイト超になりFirst Frame(`0x10 len 41 ...`)が来るが、
  FC(`30 00 00`)を返さないためECUは後続を送らず中断する。
- `src/domain/obd.cpp:4` `checkHeader()` は `data[1]==0x41` を期待するが、FFでは
  data[1]=長さ下位バイトなのでデコード失敗 →「1個だけ変なデータ」という過去の症状と整合。

### 副産物: PID 0x68 の「応答なし」も同根の可能性が高い

- `OBD.md` スキャン結果: `0x68 (Charge Air Cooler Temp)` は
  **「マスク対応だが応答なし」**と記録されている。
- 0x68の応答はセンサーマスク+複数センサー分でペイロード9バイト = **マルチフレーム必須**。
  ECUはFFを送ったが受信側が処理できず「応答なし」扱いになったと推定。
- 0x0F(IAT)は非対応確定(スキャン済み)なので、**0x68がこの車で吸気温を取る唯一の
  標準PID候補**。`docs/obd2_honda_nvan.md:116,147-151` にCivic FC1/FK8での
  0x68/0x0168実績メモあり(インタークーラー前後の2センサー)。
- VE計算の温度項(現状45℃固定仮定)を実測に置き換えられるため価値が高い。
- 将来のMode 22 DIDスキャン(ATF温度 0x2201 等、`OBD.md` 参照)もマルチフレーム前提。

### 確定: ECUは多PID要求を受け付ける(原因B確定)

§4のテスト1を実車実施。RPM(0x0C)+MAP(0x0B)の2PID要求に対し、ECUは
`41 0C [A][B] 0B [A]`の形で両PIDとも1フレームで返した（`§0 進捗状況`参照）。
Honda ECUがMode 01の多PID要求を拒否する懸念（原因A）は否定され、過去の
「1個だけ返る」不具合は受信側の実装不備（原因B）だったことが確定した。

## 3. 実装タスク

### タスク1: ISO-TP受信の最小実装 (`src/device/can.cpp`)

`canReceiveObdResponse()` を拡張、または `canReceiveIsoTp()` を新設:

1. 受信フレームの `data[0] & 0xF0` で分岐:
   - `0x00` (SF): 現行どおり。長さ = `data[0] & 0x0F`。
   - `0x10` (FF): 全長 = `((data[0]&0x0F)<<8) | data[1]`、先頭6バイトをバッファへ。
     **FCを即送信**してからCF受信ループへ。
   - `0x20` (CF): シーケンス番号 `data[0]&0x0F` を検証しつつ7バイトずつ追記。
2. FC送信:
   - 内容: `30 00 00` + パディング(BS=0: 全部送れ, STmin=0)。固定値でよい。
   - **宛先IDは受信IDから導出**: 応答ID `0x18DAF1xx` (xx=ECUアドレス) に対し
     FC送信先は `0x18DAxxF1`。ハードコードせず受信IDの下位バイトから組むこと。
   - 29-bit拡張ID (`extd=1`)。
3. タイムアウト: FF受信後、CF間タイムアウト(例: 50ms)で打ち切り。
4. バッファ: 呼び出し側に最大長を渡させる。当面64バイトあれば十分
   (多PID応答・0x68・Mode22想定)。
5. 既存の単発PID経路は回帰させない(SFは従来と同じ動作)。

規模感: 40〜60行程度。全二重・BS>0・STmin対応などの作り込みは不要。

### タスク2: 多PID要求の送信対応 (`src/device/can.cpp`)

- `canSendObdRequest(uint8_t pid)` に加え、複数PID版を追加:
  `data[0] = 1+N` (PCI: SF, 長さ=モード1+PID数N), `data[1]=0x01`, `data[2..]=PIDs`。
  N最大6(SF内)。
- **物理アドレッシング対応を推奨**: 現行は functional `0x18DB33F1` 固定。
  多PIDはfunctionalを嫌うECUがあるため、初回応答で学習したECUアドレスを使い
  `0x18DAxxF1` 宛に送るオプションを付ける。
  → **実装時に見送り。** §4テスト1でfunctionalのまま2PID要求が問題なく通ることを
  実車確認済みのため、この個体では不要と判断（`§0進捗状況`参照）。将来N>2や
  高頻度化で問題が出たら追加を検討。

### タスク3: 多PID応答パーサ (`src/domain/obd.cpp`)

- 応答ペイロードは `41 [PID_a] [data_a...] [PID_b] [data_b...] ...` の連結。
  **PIDの並び順・省略はECU任せ**なので、位置固定の `checkHeader()` は使えない。
- PID→データ長のテーブルを持ち、TLV的に順に歩くパーサを新設。
  既存デコーダ群はデータ部の解釈関数として再利用できる形に整理するとよい。
- 対応不可PIDが要求に混ざっていた場合、応答から黙って省かれるのが普通。
  欠落を許容する設計にする。

### タスク4: 0x68 の再挑戦 (タスク1完了後)

- 単発で 0x68 を要求し、マルチフレーム応答が組み上がるか確認。
- 応答フォーマット: `41 68 [サポートマスク] [A-40=センサ1℃?] ...`
  — バイト割り当ては実測で確定させること(`docs/obd2_honda_nvan.md` の
  Civic実績ではB-40がIC後、C-40がIC前。ツールによりMode 22 `0x0168` 解釈の場合あり)。
- 取れたら `OBDReading` / `ObdBlePacket` / mobile側 `_ObdReading.fromBytes()` /
  ingest / Athenaスキーマへ `iat_c` (できればIC前後2ch) を追加。
  ※ `ObdBlePacket` は `#pragma pack(1)` でオフセットをmobile側と完全一致させる
  規約あり(`OBD.md` 「送信データレイアウト」参照)。末尾追加+バージョン管理を推奨。

## 4. 検証手順

### テスト1: 原因A/Bの最終切り分け(タスク1着手前でも可能)

応答がSFに収まる2PID要求を現行受信コードのまま試す:

- 要求: `0x0C + 0x0B` (RPM+MAP) → 期待応答 `41 0C A B 0B x` (6バイト、SF)
- 両PIDが返る → ECUは多PID対応。失敗は受信側(原因B)と確定。
- `41 0C A B` のみ返る → ECUは多PID要求の先頭しか処理しない(原因A)。
  その場合タスク2/3は縮小し、タスク1(0x68とMode 22用)に注力する。

### テスト2: ISO-TP実装後

1. テスト1と同じ2PIDで回帰確認。
2. 応答がマルチフレームになる組(例: `0x0C+0x0B+0x66`)で組み立て確認。
   ログにFF/FC/CFの各フレームdumpを出すデバッグモードがあると切り分けが楽。
3. 0x68 単発。
4. 既存28PID単発ポーリングの回帰(1Hz通常モードが壊れていないこと)。

### 注意事項

- `0x66`(MAF) は単独で応答ペイロード7バイト = SF上限ぴったり。
  **他PIDと組むと必ずマルチフレーム化する**。SF内パッキングの組み合わせを
  考えるときは各PIDの応答長テーブルで事前計算すること。
- `canReceiveObdResponse` の現行ロジックは前回要求の遅延応答が残っていた場合に
  誤マッチしうる(デコード失敗で捨てられるだけだが)。多PID化に合わせ、
  応答先頭のサービスID/PIDで要求との対応を検証すると堅くなる。
- RPM+MAPが同一フレームで返る = 同時刻サンプルになるのはVE計算に好都合
  (dMAP/dt補正の位相誤差が減る)。ダイノモードのPIDセット設計時に活かす。

## 5. 本実装の先にあるもの(参考・今回のスコープ外)

- **ダイノモード**: TPS>90%等をトリガーに RPM/MAP/MAF(/IAT) を10Hz級で取得。
  リングバッファによるプリトリガー録画方式。通常時は現行1Hz。
- 解析側(Athena/Grafana)はスキーマ変更不要(iat_c追加を除く)。
  サンプリングレート混在のみ解析時に考慮。
- 目的: WOTスイープからのVEマップ作成、インマニ充填動態補正、
  月次VEマップ差分によるエンジン経年診断。CVT車のため中間RPM×高MAPセルは
  変速比通過中の短時間しか埋まらず、高レート化が効く。

---

## 6. 別セッション継続用の補足メモ

### BOARD_VERSIONの罠（v1=本番、v2=開発中）

`platformio.ini`の`default_envs`は`esp32-s3-devkitc-1-v2-develop`（v2基板向け）。
`pio run`をenv指定なしで実行するとv2向けにビルドされるが、**本番車両はv1基板**。
ビルド確認だけならv2デフォルトで問題ないが（今回の変更はCANピン配置が
v1/v2共通なので実害なし、`board_pins_v1.h`/`board_pins_v2.h`参照）、
実車への書き込み・リリースタグ付けは必ずv1向け
（`esp32-s3-devkitc-1-v1-develop`/`-v1-release`）であることを意識すること。
v1/v2は`RELEASE.md`の規約通り独立にバージョニングされる
（`config.h`の`FIRMWARE_VERSION_BASE`がBOARD_VERSIONで分岐）。

### AWS上のデータ検証方法（Athenaクエリ）

`infra/manage.ps1 output -Profile default`でTerraform管理下のリソース名
（S3バケット・Athena workgroup名・Glue DB名等）が取れる。DB名は
`replace(var.project, "-", "_")`（デフォルト`project="iot-monitor"`なら
`iot_monitor`）、workgroup名は`var.project`そのまま（`iot-monitor`）。

クエリはAWS CLIで直接投げられる:

```bash
export AWS_PROFILE=default
QID=$(aws athena start-query-execution \
  --query-string "SELECT ... FROM iot_monitor.obd_data WHERE ..." \
  --work-group iot-monitor \
  --query-execution-context Database=iot_monitor \
  --query 'QueryExecutionId' --output text)
# ポーリングしてSUCCEEDEDを待ってから
aws athena get-query-results --query-execution-id "$QID" --output json
```

**タイムゾーンの罠**: `obd_ts`はUTC epoch秒。表示は
`from_unixtime(obd_ts) AT TIME ZONE 'Asia/Tokyo'`でJST変換できるが、
**WHERE句の`timestamp 'YYYY-MM-DD HH:MM:SS'`リテラルはUTC解釈**される
（Athenaのデフォルトセッションタイムゾーン）。JST時刻で絞り込みたい場合は
必ず`-9時間`した値を書くこと（例: JST 21:30 → `timestamp '... 12:30:00'`）。
このセッション中、JST表示値をそのままリテラルに使って0件になる事故が発生した。

パーティション列（`year`/`month`/`day`/`hour`）はパーティションプロジェクション
なので、`obd_ts`の絞り込みに加えて`year='2026' AND month='08' AND day='07'
AND hour IN ('12','13')`のように明示すると確実（時刻がUTC時で日をまたぐ・
時をまたぐ場合は該当する複数の値をIN句で列挙する）。

### 2026-08-07 リリース時点の状態

- `main`は`v1.21.0`タグ済み・OTA配信済み（v1本番車両）
- バグ混入期間（JST 21:30頃〜21:57:46、`coolant_c=0`等）のデータはAthena上に
  残存させる方針（見た目で識別可能なため削除不要と判断）
- 単発PID版`canSendObdRequest()`（`can.h/.cpp`）が未使用のまま残存（§0残作業参照）
- 実走行・IGN OFF時の異常系は未検証のままリリース済み（§0残作業参照）
