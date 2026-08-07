# 引き継ぎ: ISO-TP マルチフレーム対応と多PID要求の実装

対象リポジトリ: `karuru6225/car-iot-services` / `esp32_iot_gateway`
背景: VEマップ作成のための高レート(10Hz級)OBDポーリング「ダイノモード」構想の前段として、
CAN層の制約を解消する。本書はチャット側での調査結果と実装方針の引き継ぎ。

---

## 0. 進捗状況（別PCでの継続用）

**ブランチ**: `isotp-multipid`（`main`には未マージ）
**PR**: [#6](https://github.com/karuru6225/car-iot-services/pull/6)（draft）

### 完了（コミット済み・ビルド確認済み・実車未検証）

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
   **実装・ビルド確認のみ（実車未検証）**。`kPidLengths[]`は今回RPM/MAP以外の
   27PID分の実測検証をしていないため、他PIDの長さテーブルに誤りがあると
   同一グループ内の後続PIDの値がズレる形で顕在化する可能性がある点に注意。

### 次にやること

1. `isotp-multipid`ブランチを書き込み、実車で以下を確認:
   - `[OBD] poll: OK=29/29 ... 未応答PID=0`のように全PIDが従来通り取れているか
     （バッチ化後の回帰確認）。
   - `[OBD]`/`[OBD2]`ログの各値が、直前のバッチ化前ビルドで見た値と矛盾しないか
     （especially 0x24ワイドバンドO2・0x66 MAF Alt等、他PIDと組み合わさった際に
     `kPidLengths[]`のズレで値が崩れていないか）。
   - サイクル間隔が体感で速くなっているか（29リクエスト→5リクエストの効果）。
2. 問題があれば`kPidLengths[]`の該当PIDの長さを見直す。
3. （任意・実車検証）過給時のログを追加取得し、`iat_c`/`iat2_c`と
   インタークーラー前後の対応をいずれ確定させる。

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
