# OBD-II 統合設計

フェーズ1（実車接続確認・PID スキャン）の結果と、フェーズ2以降の実装設計をまとめる。  
プロトコル詳細・ブレッドボード試験手順は `CAN_REFERENCE.md` を参照。

---

## 実車スキャン結果

**スキャン実施日:** 2026-05-23  
**電源状態:** IGN ON（エンジン未始動）  
**車種:** Honda N-VAN JJ1/JJ2  

**ビットマスク raw 値（getSupportedPidMask で取得）:**

| 問い合わせ PID | 取得マスク | 対象範囲 |
|-------------|-----------|---------|
| 0x00 | `0xB63CA813` | 0x01〜0x20 |
| 0x20 | `0x9005A011` | 0x21〜0x40 |
| 0x40 | `0x72C08C01` | 0x41〜0x60 |
| 0x60 | `0x07114001` | 0x61〜0x80 |

**Priority 1 スキャン結果:**

| PID | 名称 | 結果 | 値（IGN ON） |
|-----|------|------|-------------|
| 0x04 | Engine Load | **✓ OK** | 0% |
| 0x05 | Coolant Temp | ✗ 非対応 | — |
| 0x0B | MAP | **✓ OK** | 101 kPa (boost: 0 kPa) |
| 0x0C | RPM | **✓ OK** | 0 rpm |
| 0x0D | Speed | **✓ OK** | 0 km/h |
| 0x11 | Throttle | **✓ OK** | 17% |

**Priority 2 スキャン結果:**

| PID | 名称 | 結果 | 値（IGN ON） |
|-----|------|------|-------------|
| 0x0A | Fuel Pressure | ✗ 非対応 | — |
| 0x0E | Ignition Adv | **✓ OK** | 0.0° BTDC |
| 0x0F | Intake Temp | ✗ 非対応 | — |
| 0x10 | MAF | ✗ 非対応 | — |
| 0x2F | Fuel Level | ✗ 非対応 | — |
| 0x33 | Baro | **✓ OK** | 101 kPa |
| 0x42 | ECU Voltage | **✓ OK** | 11.692 V |
| 0x5C | Oil Temp | ✗ 非対応 | — |
| 0x5E | Fuel Rate | ✗ 非対応 | — |

**ビットマスクから確認できるその他のサポート PID（未クエリ）:**

| PID | 名称 |
|-----|------|
| 0x06 | Short Term Fuel Trim Bank 1 |
| 0x07 | Long Term Fuel Trim Bank 1 |
| 0x13 | O2 Sensors Present |
| 0x15 | O2 Sensor B1S2 |
| 0x1C | OBD Standards |
| 0x1F | Time Since Engine Start |
| 0x21 | Distance Traveled with MIL On |
| 0x24 | O2 Sensor 1 (Equivalence Ratio / Voltage) |
| 0x2E | Commanded Evap Purge |
| 0x30 | Warm-ups Since Codes Cleared |
| 0x31 | Distance Since Codes Cleared |
| 0x3C | Catalyst Temperature Bank 1 |
| 0x43 | Absolute Load Value |
| 0x44 | Commanded Air-Fuel Equivalence Ratio |
| 0x47 | Absolute Throttle Position B |
| 0x49 | Accelerator Pedal Position D |
| 0x4A | Accelerator Pedal Position E |
| 0x51 | Fuel Type |
| 0x55 | Short-term Secondary O2 Trim |
| 0x56 | Long-term Secondary O2 Trim |

**Priority 3 スキャン結果（0x61〜0x80）:**

Mask[0x60]=0x07114001 （エンジン始動後、949 rpm 時に取得）

| PID | 名称 | 結果 | 値 |
|-----|------|------|---|
| 0x61 | Torque Demand | ✗ 非対応 | — |
| 0x62 | Torque Actual | ✗ 非対応 | — |
| 0x63 | Torque Reference | ✗ 非対応 | — |
| 0x66 | MAF Alt | **✓ OK** | sensors=0x01, MAF1=**1.69 g/s**（アイドル 949 rpm） |
| 0x67 | Coolant Temp Alt | **✓ OK** | sensors=0x03, **S1=72°C**（冷却水）, S2=17°C（外気？） |
| 0x68 | Charge Air Cooler Temp | !! マスク対応だが応答なし（※後述） | — |
| 0x6C | Commanded Throttle Actuator | !! マスク対応だが応答なし | — |
| 0x6E | Boost Ctrl | ✗ 非対応 | — |
| 0x70 | Boost Pressure Control | !! マスク対応だが応答なし | — |
| 0x72 | Turbocharger RPM | !! マスク対応だが応答なし | — |
| 0x7F | Engine Run Time | ✗ 非対応 | — |

**Priority 4 スキャン結果（0x81〜0xA0）:**

Mask[0x80]=0x00000002 → 0x9F のみマスク対応

| PID | 名称 | 結果 | 備考 |
|-----|------|------|------|
| 0x9F | Emission Req | !! マスク対応だが応答なし | — |
| 他全て | — | ✗ 非対応 | — |

**「!! マスク対応だが応答なし」について:**  
Honda ECU が variant 共通のビットマスクを返しているが、この個体（JJ1/JJ2）では実際のデータを持たない PID が含まれている可能性が高い。timeout を延ばしても変わらない可能性がある。

**※0x68について（`CONTEXT_ARCHIVE.md`の「ISO-TPマルチフレーム対応・多PID要求」参照）:** 上記「応答なし」はISO-TP（ISO 15765-2）
マルチフレーム受信未実装が原因と判明。0x68の応答ペイロードは9バイトでSF上限（7バイト）を
超えるため、Flow Control未送信でECUが送信を打ち切っていた。マルチフレーム受信実装後は
`41 68 03 4A 49 00 00 00 00`（Sensor1=34°C, Sensor2=33°C）が取得できることを実車で確認済み。

**確定した取得可能データ一覧（フェーズ2 実装対象・新基板 2026-06-01 実機確認済み）:**

**全29PID実装済み**（`domain/obd.h/.cpp`・`service/obdpoll.cpp`、実装差分は本ドキュメント後半の「domain/obd.h データ構造・デコード関数」参照）。

| PID | 名称 | デコード式 | 備考 |
|-----|------|-----------|------|
| 0x04 | Engine Load | A×100/255 % | 負荷監視 |
| 0x06 | Short Term Fuel Trim | (A-128)×100/128 % | 空燃比補正（短期） |
| 0x07 | Long Term Fuel Trim | (A-128)×100/128 % | 空燃比補正（長期） |
| 0x0B | MAP | A kPa | ブースト計算のベース |
| 0x0C | RPM | (A×256+B)/4 rpm | 全体制御 |
| 0x0D | Speed | A km/h | — |
| 0x0E | Ignition Timing | A/2-64 °BTDC | — |
| 0x0F | — | — | 非対応 |
| 0x11 | Throttle A | A×100/255 % | — |
| 0x15 | O2 Sensor B1S2 (NB) | A=電圧(A/200 V), B=燃料トリム | — |
| 0x1F | Time Since Engine Start | A×256+B 秒 | — |
| 0x21 | Distance with MIL | A×256+B km | DTC 点灯時走行距離 |
| 0x24 | O2 Sensor 1 (WB) | ratio=(A×256+B)×2/65536, V=(C×256+D)×8/65536 | ワイドバンドセンサー |
| 0x2E | Evap Purge | A×100/255 % | — |
| 0x30 | Warmups Since Cleared | A 回 | — |
| 0x31 | Distance Since Cleared | A×256+B km | — |
| 0x33 | Baro | A kPa | ブースト=MAP-Baro |
| 0x3C | Catalyst Temp B1S1 | (A×256+B)/10-40 °C | — |
| 0x42 | ECU Voltage | (A×256+B)/1000 V | — |
| 0x43 | Absolute Load | (A×256+B)×100/255 % | 0x04 の絶対値版 |
| 0x44 | Commanded AFR (Lambda) | (A×256+B)×2/65536 | — |
| 0x47 | Throttle B | A×100/255 % | — |
| 0x49 | Accel Pedal D | A×100/255 % | アクセル物理開度 |
| 0x4A | Accel Pedal E | A×100/255 % | アクセル物理開度 |
| 0x51 | Fuel Type | A (1=ガソリン) | — |
| 0x55 | Sec O2 Trim B1 (ST) | (A-128)×100/128 % | — |
| 0x56 | Sec O2 Trim B1 (LT) | (A-128)×100/128 % | — |
| 0x66 | MAF Alt | (B×256+C)/32 g/s | **燃費推算のソース**（0x10 の代替） |
| 0x67 | Coolant Temp Alt | B-40 °C (Sensor1) | **0x05 の代替** |
| 0x68 | Charge Air Cooler Temp | B-40 / C-40 °C (Sensor1/2) | 吸気温。ISO-TPマルチフレーム必須。IC前後どちらがSensor1/2かは未確定 |

燃費推算: `fuelRateLph = mafGs / (14.7 × λ × 0.745) × 3.6`（λ=commandedAfr、0.5〜2.0の範囲外はλ=1固定）

---

## プロトコル確認事項（実車で確認済み）

### 1. 29ビット拡張アドレッシングが必須

Honda N-VAN は標準の 11ビット OBD-II アドレス（0x7DF）に応答しない。  
29ビット SAE J1939 形式を使う必要がある。

| 項目 | 値 |
|------|---|
| リクエスト CAN ID | `0x18DB33F1`（extd=1, functional addressing） |
| 応答 CAN ID | `0x18DAF10E`（extd=1, Honda エンジン ECU = アドレス 0x0E） |
| ビットレート | 500 kbps |

### 2. 11ビットアドレッシングは無応答

`0x7DF` 機能的アドレッシングは TX 自体は成功（car が CAN レベルでは ACK する）するが、  
OBD-II 応答フレームは一切来ない（確認済み）。

### 3. IGN ON が必須

| 電源状態 | 動作 |
|---------|------|
| 電源 OFF / ロック | CAN ゲートウェイが起きない。TX エラーカウンタが急増（bus_err が数秒で数万件）。RX は一切得られない |
| **IGN ON（エンジン未始動）** | **通信安定。全 PID への応答あり（RPM=0、速度=0）。スキャンに最適** |
| エンジン始動後 | 実測値が取得可能 |

### 4. 取得できないデータの代替手段

冷却水温（0x05）・MAF（0x10）・燃料流量（0x5E）・燃料残量（0x2F）・油温（0x5C）は Mode 01 非対応。  
ただし以下の代替で対応済み:

| 非対応 PID | 代替 | 状態 |
| --- | --- | --- |
| 0x05 Coolant Temp | 0x67 Sensor1 (B-40°C) | **取得可能・確認済み** |
| 0x10 MAF | 0x66 MAF Alt ((B×256+C)/32 g/s) | **取得可能・確認済み** |
| 0x5E Fuel Rate | 0x66 から推算: `mafGs/(14.7×λ×0.745)×3.6` | **推算で対応** |
| 0x2F Fuel Level | Mode 22 探索が必要 | 未対応 |
| 0x5C Oil Temp | Mode 22 探索が必要 | 未対応 |

Mode 22（Honda 独自拡張）で燃料残量・油温を取得できる可能性があるが未確認。フェーズ2では Mode 01 で取得できる値のみを実装する。

### Mode 22 実機テスト候補

Mode 22 は UDS（ISO 14229）サービス 0x22（ReadDataByIdentifier）。  
リクエスト: `03 22 [DID_HI] [DID_LO] 00 00 00 00`  
正常レスポンス先頭: `04 62 [DID_HI] [DID_LO] [VALUE]`  
否定レスポンス: `03 7F 22 31`（NRC=0x31 = DID 非サポート）

**アドレッシングは物理宛てに切り替える**: Mode 01 が使う `0x18DB33F1`（機能アドレッシング＝
ブロードキャスト）はどの ECU が応答するか不明な探索に向くが、UDS サービス（ReadDataByIdentifier
含む）は機能アドレッシングに応答しない ECU 実装が多い。エンジン ECU のアドレスは応答 ID から
`0x0E` と判明済みのため、Mode 22 リクエストは物理アドレッシング `0x18DA0EF1`
（`0x18DA` | 対象ECU`0x0E` | 送信元`0xF1`）を使うのが定石。応答 ID は変わらず
`0x18DAF10E`（`can.cpp` の `CAN_RESP_MASK`/FC 宛先組み立てと同じ形）。

| DID | データ | デコード式（暫定） | 優先度 | 状態 |
| --- | ------ | ---------------- | ------ | ---- |
| `0x2201` | ATF 温度 | byte27(AA) - 40 [°C] | 高 | **実装済み・実車未テスト**（`domain/obd.cpp` の `obdDecodeAtfTemp()`、`obdPoll()` から常時問い合わせ） |
| 未確定 | 燃料残量 | 不明 | 高 | DID スキャン要 |
| 未確定 | 油温 | 不明 | 中 | DID スキャン要 |

**DID スキャン機能（実装済み・全域一括スキャンに簡略化済み）**: `service/diddscan.h/.cpp` に `didScanRun()`
を実装。指定範囲を `22XXYY` で総当たりし、正常応答（`62`）と NRC `0x22`/`0x33`（存在するが今は読めない／
認証必要）のみを記録する（`0x31` 非対応・タイムアウトは件数カウントのみ）。OLED メニュー「OBD > DID Scan」
選択で即座に全域 `0x0000`〜`0xFFFF` を一括スキャンする（プリセット範囲選択メニューは全域スキャンを既に
完走済みのため2026-08-20に廃止、`kDidScanPresets[]`・`DidScanPreset`・`DID_SCAN_SELECT`状態も削除した）。
スキャン中は BTN1 長押しで中断可能。正常応答（OK）のみシリアルログに出力し、NRC ヒットはログに出さない
（OLED上のヒット一覧には引き続き両方表示する）。  
燃料補給前後・冷間/暖機後でデータが変化する DID を絞り込む運用は今後の課題（差分比較の自動化は未実装）。

---

## フェーズ1.5: 生 CAN 探索（完了・OBD-II ポートでは不可と判明）

**OBD-II（Mode 01/22）では公開されないデータが F-CAN バス上を流れている可能性がある。**  
フェーズ2 の実装に入る前に LISTEN_ONLY で生フレームを観察し、取得可能なデータを確定させる。

### 目的

- OBD-II 非対応データ（燃料残量・油温・エアコン等）が生フレームで流れているか確認
- 既知 CAN ID（みんカら N-VAN 実測）の存在を確認
- 未知フレームの CAN ID とデータパターンを記録し、将来的なデコードの足がかりにする

### 既知の N-VAN F-CAN フレーム（みんカラ実測、信頼性: 実測）

| CAN ID  | 内容         | デコード（暫定）          | 備考                            |
|---------|--------------|--------------------------|--------------------------------|
| `0x158` | 車速         | 推定: バイト値 ÷ 定数    | OBD 0x0D と突き合わせで確認可能 |
| `0x1DC` | エンジン RPM | 推定: 2バイト ÷ 定数     | OBD 0x0C と突き合わせで確認可能 |
| `0x324` | 冷却水温     | 推定: `d0 - 40` [°C]     | OBD 0x67 と突き合わせで確認可能 |

OBD で既に取れる値と突き合わせることでデコード式を検証できる。

### blank.cpp の変更点

```diff
- #define CAN_MODE TWAI_MODE_NO_ACK
+ #define CAN_MODE TWAI_MODE_LISTEN_ONLY
```

送信ロジック（1秒ごとの TX 部分）を削除し、受信のみにする。  
全フレームをシリアルに出力して観察する。

```cpp
void loop() {
  twai_message_t rx = {};
  if (twai_receive(&rx, pdMS_TO_TICKS(10)) == ESP_OK) {
    Serial.printf("%s id=0x%08lX len=%d  ",
      rx.extd ? "EXT" : "STD", rx.identifier, rx.data_length_code);
    for (int i = 0; i < rx.data_length_code; i++)
      Serial.printf("%02X ", rx.data[i]);
    Serial.println();
  }
}
```

### 確認手順

1. blank env でビルド・書き込み
2. シリアルモニタを開く（115200 bps）
3. 車の IGN ON（エンジン未始動）
4. 出力される CAN ID を記録する
5. エンジン始動・アクセル操作・エアコン ON/OFF などで変化するフレームを観察

### 記録すべき項目

- 観測された CAN ID 一覧（STD/EXT 別）
- 既知 ID（0x158/0x1DC/0x324）の存在確認
- RPM・車速・水温と突き合わせて一致したフレーム
- エアコン・ターボ・燃料系に関係すると思われる変化パターン

### 実施結果（2026-06-01）

**結論: OBD-II ポートでは生 F-CAN トラフィックは観察できない。**

LISTEN_ONLY モードおよびスキャン後の receive-only モードで、アンビエントな CAN フレームはゼロ。
OBD-II ポートは CAN ゲートウェイ経由で診断専用に隔離されており、ECU への OBD リクエスト送信時のみ応答フレームが返ってくる構成。
みんカラ実測の 0x158 / 0x1DC / 0x324 等は OBD-II ポートからは見えない。

**生 F-CAN 観察には OBD-II ポートではなく、車内の F-CAN バス配線に直接タップする必要がある。**

---

## アーキテクチャ統合概要（実装済み）

既存の 3 層構造への追加。依存ルール（device ← domain ← service）を守る。GU0 コネクタ
（GPIO4/5/6）に接続した MCP2562FD 経由で通信する。GU1（GPIO7/8/9）は LTE（SIM7080G）が
使用中のため触らない（当初ドラフトは GU1 想定だったが、実配線確認の結果 GU0 に変更）。

導入当初は専用モード `OperationMode::CONTINUOUS_OBD`（メニュー "Continuous OBD" または Shadow
`override_next_mode: "continuous_obd"` から入る）として実装したが、後日の整理で既存 CONTINUOUS
モードにOBDポーリングが統合された。専用モード・専用メニュー項目・専用Shadow override値は廃止
されており、現在は `CONTINUOUS`（5分境界待機ループ・BLE notify）に入れば自動でOBDポーリングも
1秒間隔で動作する。

```
config.h               変更  （導入時のみ）OperationMode に CONTINUOUS_OBD を追加、後日 CONTINUOUS に統合され廃止
device/can.h/.cpp      新規  TWAI ラッパー（GPIO4=RX, GPIO5=TX, GPIO6=EN, 500kbps）
domain/obd.h/.cpp      新規  OBDReading 構造体・PID デコード関数
service/obdpoll.h/.cpp 新規  全PID逐次ポーリング（canInit()済み前提）
device/oled.h/.cpp     変更  oledShowObdData() を追加（1画面）
main.cpp               変更  setOperationMode() でモード遷移集約・CAN init/deinit・1秒間隔ポーリング
```

**今回のスコープ: OBD 取得データは AWS へ publish しない。** ログ出力（`logger.printf()`）と
OLED 表示にのみ使う。`domain/telemetry.h/.cpp` ・ `service/pubqueue.h/.cpp` は変更していない。
AWS への送信方法（既存パイプライン統合が妥当か等）は別途検討する。

---

## device/can.h インターフェース（実装済み）

Honda N-VAN は 29ビット拡張アドレッシングが必須（11ビット 0x7DF は無応答）。

```cpp
#pragma once
#include <stdint.h>

bool canInit();   // 冪等: 既に起動済みなら即 true
void canDeinit(); // 未起動でも安全に呼べる（GPIO6 を確実に LOW にする）

enum class ObdRecvResult : uint8_t { Ok, Timeout, NegativeResponse, Error };
ObdRecvResult canReceiveObdResponse(uint8_t *data, uint8_t *dlc, uint32_t timeoutMs = 100,
                                     uint8_t maxLen = 8, uint8_t *nrcOut = nullptr);
```

否定応答（`7F [SID] [NRC]`）を受信した場合は `NegativeResponse` を返し、`nrcOut` が
非nullptrならNRCを書き込む。既存呼び出し側は `== ObdRecvResult::Ok` で成否判定する。

ピンは `boardPins()` 経由で取得する（ハードコードしない）:
- `CAN_RX_PIN = boardPins().gu01Pin`（GPIO5、MCP2562FD RXD 側）
- `CAN_TX_PIN = boardPins().gu00Pin`（GPIO4、MCP2562FD TXD 側）
- `CAN_EN_PIN = boardPins().gu0EnPin`（GPIO6、AO3401A ゲート）

`CAN_REFERENCE.md`「7. ブレッドボード単体試験」の配線（GPIO5=TXD, GPIO4=RXD）そのまま。当初ドラフトは
GU1（GPIO7/8/9）想定で TX/RX が逆だったため、実装時に修正した。

### バスオフ自動リカバリ（`can.cpp` 内にカプセル化）

- 送信前に `twai_get_status_info()` を確認し、`TWAI_STATE_BUS_OFF` なら
  `twai_initiate_recovery()` のみ実行（軽量、ドライバ再インストール不要）
- 連続送信失敗が閾値（20回）を超えたら `canDeinit()` → `canInit()` のフル再初期化に
  エスカレーション
- 呼び出し側（`obdpoll.cpp`）は戻り値 `bool` だけ見ればよい

### CAN init/deinit のタイミング

`measure()`（5分周期）ではなく、`main.cpp` の `setOperationMode()` が `CONTINUOUS_OBD`
への出入りを検知して呼ぶ。1秒間隔ポーリングのたびに init/deinit すると TWAI ドライバの
install/uninstall・GPIO 電源トグルのオーバーヘッドが大きいため。

---

## domain/obd.h データ構造・デコード関数（実装済み・全29PID対応）

**実車スキャン結果を反映。非対応 PID（0x05水温・0x10 MAF・0x5E燃料流量等）は除外。**

```cpp
struct OBDReading {
  // 初期実装分（10PID）
  uint16_t rpm;           // 0x0C: (A*256+B)/4 [rpm]
  uint8_t  speedKmh;     // 0x0D: A [km/h]
  uint8_t  loadPct;      // 0x04: A*100/255 [%]
  uint8_t  mapKpa;       // 0x0B: A [kPa 絶対圧]
  uint8_t  baroKpa;      // 0x33: A [kPa]
  int8_t   boostKpa;     // mapKpa - baroKpa [kPa]（obdComputeDerived()で計算）
  uint8_t  throttlePct;  // 0x11: A*100/255 [%]
  float    timingDeg;    // 0x0E: A/2.0-64.0 [°BTDC]
  float    ecuVoltage;   // 0x42: (A*256+B)/1000.0 [V]
  float    mafGs;        // 0x66: (B*256+C)/32 [g/s]（0x10 非対応のため代替）
  int16_t  coolantC;     // 0x67 Sensor1: B-40 [°C]（0x05 非対応のため代替）
  float    fuelRateLph; // MAF 推算: mafGs / (14.7×0.745) × 3.6 [L/h]（obdComputeDerived()で計算）

  // 追加実装分（18PID・20フィールド。デコード式は「確定した取得可能データ一覧」参照）
  float    stftPct, ltftPct;                          // 0x06, 0x07
  float    o2B1s2V, o2B1s2TrimPct;                 // 0x15
  uint16_t engineRunTimeSec;                         // 0x1F
  uint16_t milDistanceKm;                             // 0x21
  float    o2S1Ratio, o2S1Voltage;                  // 0x24
  uint8_t  evapPurgePct;                              // 0x2E
  uint8_t  warmupsSinceCleared;                       // 0x30
  uint16_t distanceSinceClearedKm;                   // 0x31
  float    catalystTempC;                             // 0x3C
  float    absoluteLoadPct;                           // 0x43
  float    commandedAfr;                               // 0x44
  uint8_t  throttleBPct;                              // 0x47
  uint8_t  accelPedalDPct, accelPedalEPct;        // 0x49, 0x4A
  uint8_t  fuelType;                                   // 0x51
  float    secO2TrimStPct, secO2TrimLtPct;      // 0x55, 0x56

  bool     valid;
  time_t   ts;

  // 末尾追加分（1PID・2フィールド。ObdBlePacketとのオフセット互換のため末尾に配置）
  int16_t  iatC, iat2C;                               // 0x68（IC前後どちらか未確定）

  // kPids[]（service/obdpoll.cpp）配列順のPIDごとのデコード成否ビットマスク
  uint32_t validMask;
};
```

デコード関数は29個（PIDごとに1関数、0x15と0x24のみ1関数で2フィールドを埋める。
0x68も1関数で2フィールドを埋める）。
device/service に依存しない純粋関数。`can.cpp` の `canReceiveObdResponse()` が ISO-TP PCI バイトを
剥がしたペイロード（`41 PID data...`）を渡してくる前提。共通ルール:
`data[0] != 0x41` または `data[1] != 要求PID` または `dlc` が必要バイト数未満なら false
（`obd.cpp` 内 `checkHeader()` ヘルパーが判定し、一致すればペイロード先頭（A）へのポインタを返す）。

---

## service/obdpoll.h ポーリング関数（実装済み）

```cpp
OBDReading obdPoll(); // 全29PID逐次問い合わせ（canInit()済み前提）
```

`measure()`/`publish()`（5分周期前提）とは呼び出し契約が異なるため、`service/monitor.h/.cpp`
に混在させず独立ファイルにした。タイムアウトは **50ms**（10PID時代の100msから短縮）。
実車では正常応答が数十msで返る実績があるため正常系には影響せず、28PID化に伴い
IGN OFF等の異常時の最悪サイクル時間（28×タイムアウト）を28×50ms=1.4秒程度に抑える狙い。
ECUへの負荷は読み取り専用のMode01のみで、市販スキャンツールと同程度の頻度のため問題ない
という前提（実車でのフェーズ1スキャンで多数PID問い合わせ済みだが異常は確認されていない）。

取得結果は `logger.printf()` でのログ出力と `oledShowObdData()` にのみ使う。
追加18PID分は既存の10PIDサマリ行とは別に `[OBD2]` タグの行でログ出力する
（1行が長大になるのを避けるため）。OLED表示（`oledShowObdData()`）は初期実装の10項目のみを
表示し、追加18項目はログ出力のみで表示は行わない。

**AWS への送信は今回のスコープ外**（`queue.pushObd()` 相当は未実装）。将来的に既存
パイプラインへ統合する場合は、`domain/telemetry.h/.cpp` の `ITelemetryEncoder` に
`encodeObd()` を追加し、`service/pubqueue.h/.cpp` に `EntryType::Obd` と `ObdEntry`
（固定小数点構造体）を追加する形になる想定。

---

## main.cpp 統合（実装済み）

OBDポーリングは導入時に専用モード `OperationMode::CONTINUOUS_OBD` として実装したが、
後日の整理で既存の `CONTINUOUS` モードに統合された。現在は `CONTINUOUS` に入っている間、
`runContinuousLoop()` の既存 1秒間隔ブロック（`lastNotify`）に相乗りする形で
`obdPoll()` → `oledShowObdData()` が毎ティック呼ばれる（CAN の init/deinit は
`setOperationMode()` が `CONTINUOUS` への出入りで行う）。専用メニュー項目・専用Shadow
override値・`CONTINUOUS_OBD` 専用の画面占有ロジックは廃止済み。

### モードへの入り方

`CONTINUOUS` モードへの入り方（OLED メニュー "Continuous" 項目。AWS Shadow の
`override_next_mode` は `"timed_continuous"` のみ対応で、素の `CONTINUOUS` への
override値は存在しない）に従う。OBD専用の入口は存在しない。

---

## OLED レイアウト（実装済み・1画面）

SSD1306 128×64、TextSize=1（6×8px、最大 21 文字/行）。当初ドラフトは2ページ構成だったが、
`runContinuousLoop()` の既存カウントダウン表示との統合を単純にするため1画面にまとめた。

```
+──────────────────────+
|RPM:949    0 km/h     |
|TPS: 14% Load: 26%    |
|MAP: 40kPa CLT: 72C   |
|IGN:+1.5  ECU:14.29V  |
|MAF:1.69g/s BST:-61kPa|
|BTN1 long: sleep      |
+──────────────────────+
```

`oledShowObdData()`（`device/oled.h/.cpp`）が1秒間隔で再描画される。応答なし時は
"OBD: no response" を表示する。

---

## BLE Notify送信（実装済み・スマホアプリ表示用）

CONTINUOUS_OBDの1秒ティックで取得した`OBDReading`（29項目・約91バイト）を、既存のBLE
Peripheral（`device/ble_peripheral.h/.cpp`）経由でスマホアプリ（`mobile/lib/main.dart`）に
表示する。デフォルトBLE ATT MTU（23バイト、ペイロード20バイト）ではデータが収まらないため、
MTU拡張には頼らず「制約に収まる分だけ詰めて複数回に分けて送る」発想でチャンク分割する。

### 送信データレイアウト（`domain/obd.h`の`ObdBlePacket` + TLV拡張フィールド領域）

`OBDReading`をそのまま`memcpy`するとコンパイラのパディングに依存してしまうため、送信専用の
パディングなし構造体（`#pragma pack(push,1)`）に変換してから送る（`obdReadingToBlePacket()`、
`domain/obd.cpp`）。**ヘッダ（メタ情報）とボディ（実データ）に分離**している:

```text
ヘッダ: schemaVersion(1) headerLen(1) extOffset(1) valid(1) validMask(4)   … 合計8バイト
ボディ: rpm … iat2C（OBDReadingと同一フィールド順、bool は uint8_t、time_t は uint32_t）
```

- `schemaVersion`（`OBD_BLE_SCHEMA_VERSION`固定値）: ボディのレイアウトバージョン。
  バージョンによって以降の解釈自体が変わりうるため一番先頭に置く。「サイズは同じだが意味が
  変わった」変更は`headerLen`/`extOffset`だけでは検出できないため、アプリ側は自分が対応している
  バージョンと不一致なら`debugPrint()`で警告を出す。ただし`headerLen`/`extOffset`によって
  ヘッダの拡張・TLV拡張領域の位置は自己記述化済みで、`schemaVersion`不一致が実際に問題になる
  のは「ボディのレイアウトを直接変えた」場合のみ（運用ルールとしてボディは増減させない前提の
  ためレアケース）なので、パース自体は拒否せず継続する（`mobile/lib/models/obd_reading.dart`
  の`ObdReading.fromBytes()`参照）。
- `headerLen`（`offsetof(ObdBlePacket, rpm)`固定値）: ヘッダ部分の全長。アプリ側はここから
  ボディの開始位置（`bytes[headerLen]`）を逆算できるため、将来ヘッダにフィールドを追加しても
  ボディ側オフセットの定数を直さずに済む。
- `extOffset`（`sizeof(ObdBlePacket)`固定値）: TLV拡張フィールド領域の開始位置。値そのものが
  `bytes[extOffset]`という形でそのままインデックスとして使える（「サイズ」ではなく「位置」を
  表す名前にしている）。
- `valid`/`validMask`: 「後続のボディをどう解釈すべきか」を示すメタ情報のため、実データより
  先に読める位置（ヘッダ側）に置いている。`validMask`は`kPids[]`配列順のPIDごとのデコード成否。

合計98バイト固定（ヘッダ8+ボディ90。オフセットは`domain/obd.h`のコメント・
`mobile/lib/models/obd_reading.dart`の`ObdReading.fromBytes()`と完全一致させること）。

**ボディは増減させない**（フィールドを足すたびにアプリ側の固定オフセット読み取りが
全部ズレて壊れるため）。ATF温度（Mode22 DID 0x2201）等、今後も増減しうる値はコア構造体の
直後に連結するTLV拡張フィールド領域に置く（`obdEncodeExtFields()`、`domain/obd.h`の
`ObdExtFieldId`）:

```text
[extCount:1] ([fieldId:1][len:1][data:len]) × extCount
```

アプリ側（`ObdReading.fromBytes()`）は知らない`fieldId`を`len`分読み飛ばすため、ファーム・
アプリいずれかだけを更新しても壊れない（新フィールド追加時、ファーム側は`obdEncodeExtFields()`
に1行足すだけ、アプリ側は`fromBytes()`のswitchに1caseとフィールド追加だけで済む）。

**ボディのパース失敗とTLV拡張領域は独立**: `ObdReading.fromBytes()`はボディを`_Reader`で
順に読み進める際、読み取り位置が`extOffset`を超えたら例外（`_ReaderOverrunException`）を投げて
即座に打ち切る（境界がズレた以上、それ以降のフィールドを読み進めても無意味な値にしかならない
ため）。ただし`extOffset`自体はヘッダの一部として別に読み取り済みで、ボディの内部構造とは
無関係に信頼できる値のため、ボディのパースが失敗（境界超過・読み足りない）した場合でも、
TLV拡張領域だけは`bytes[extOffset]`から独立してパースを試みる。ボディが壊れた場合は
`valid=false`・各フィールドはデフォルト値（境界を超える前に読めた分はその値のまま）で返り、
拡張フィールド側は正しく取得できる可能性がある。

### CRC-8（伝送破損検出）

コア構造体+TLV拡張フィールド領域の全バイトの末尾に、CRC-8（多項式`0x07`、初期値`0x00`、
CRC-8/SMBUS準拠）を1バイト付加してから送信する（`obdCrc8()`、`domain/obd.h`）。
アプリ側（`ObdReading.fromBytes()`）は同一アルゴリズムで再計算し、不一致なら以降の処理を
一切行わず即座に`null`を返す。`schemaVersion`不一致（ソフトウェア側の定義ズレ）とは性質が
異なる、BLE伝送中のビット化けという物理的な異常を検出するためのもの。

### チャンクフォーマット

```text
[0]     : seq   (uint8, 0-indexed)
[1]     : total (uint8, 総チャンク数)
[2..]   : payload（最大18バイト、コア構造体98バイト+TLV拡張領域+CRC-8(1バイト)を18バイトずつ分割）
```

`device/ble_peripheral.cpp`の`BlePeripheral::notifyObd()`が`MEAS_OBD_UUID`
（Notify、認証不要、既存の計測サービスに相乗り）へ`total`回連続で`notify()`する。

### アプリ側の再構成（`mobile/lib/ble/obd_chunk_assembler.dart`）

- `ObdChunkAssembler.add()`が`seq`ごとに`Map<int, Uint8List>`へ格納。`seq==0`または`total`が
  前回と食い違ったら前回分を破棄して集め直す（パケット取りこぼし時は次サイクルで自然に復帰する
  想定、タイムアウト等の複雑なリトライ処理はあえて入れていない）
- `total`個揃ったら結合して`ObdReading.fromBytes()`（`mobile/lib/models/obd_reading.dart`）で
  パースし、`ObdCard`（`mobile/lib/widgets/obd_card.dart`）が`ObdMetric.values`を
  `GridView.count`で表示

### 注意点

- `obdTick()`内でCANポーリング（最大1.4秒）→OLED表示→BLE Notify呼び出しが全て同期・
  シングルタスクで実行される（`main.cpp`参照）。BLE Notifyの呼び出し自体はNimBLEの送信
  キューに積むだけの軽い処理だが、5回連続で呼ぶ分メインループの占有時間はわずかに伸びる
- 既存の`onConnect`でのコネクションパラメータ（400-800×1.25ms=500-1000ms）はBLEスキャンとの
  共存のために設定されたもので、OBD用に変更していない。1秒間隔の送信と大きくズレてはいないが、
  厳密な同期は保証されない

---

## 実装順序（完了）

各ステップで `pio run -e esp32-s3-devkitc-1-v2-develop` のビルドが通ることを確認してから
コミット。

| # | 対象ファイル | 変更内容 | 状態 |
|---|------------|---------|------|
| 1 | `config.h` | `OperationMode::CONTINUOUS_OBD` 追加 | 完了 |
| 2 | `device/can.h/.cpp` 新規 | TWAI ラッパー（29-bit アドレッシング、GU0） | 完了 |
| 3 | `domain/obd.h/.cpp` 新規 | OBDReading 構造体・デコード関数 | 完了 |
| 4 | `service/obdpoll.h/.cpp` 新規 | 全PID逐次ポーリング | 完了 |
| 5 | `main.cpp` | `setOperationMode()`・CAN init/deinit・1秒間隔ポーリング | 完了 |
| 6 | `service/shadow.h/.cpp` | `override_next_mode="continuous_obd"` 対応 | 完了 |
| 7 | `service/menu.cpp` | `"Continuous OBD"` メニュー項目 | 完了 |
| 8 | `device/oled.h/.cpp` | `oledShowObdData()`（1画面） | 完了 |
| 9 | `domain/obd.h/.cpp` | `ObdBlePacket`構造体・変換関数 | 完了 |
| 10 | `device/ble_peripheral.h/.cpp` | `MEAS_OBD_UUID`・`notifyObd()`（チャンク分割） | 完了 |
| 11 | `main.cpp` | `obdTick()`に`notifyObd()`呼び出し追加 | 完了 |
| 12 | `mobile/lib/main.dart` | チャンク受信・再構成・`_ObdCard`表示 | 完了 |

**未実装（今回のスコープ外）**: AWS への publish（`domain/telemetry`・`service/pubqueue`
統合）。送信方法は別途検討する。

---

## Mode 22 (UDS) 実装（完了）

「Mode 22 実機テスト候補」節の方針に基づき、DID `0x2201`（ATF油温）の常時取得と、
未確定DID（燃料残量・油温）を探すための総当たりスキャン機能を実装した。

| # | 対象ファイル | 変更内容 | 状態 |
|---|------------|---------|------|
| 1 | `device/can.h/.cpp` | `canSendObdRequestUds()` 追加（物理アドレッシング`0x18DA0EF1`固定）。受信は既存`canReceiveObdResponse()`を流用 | 完了 |
| 2 | `domain/obd.h/.cpp` | DID `0x2201`用ヘッダチェック（`checkUdsHeader()`）・`obdDecodeAtfTemp()`・`OBDReading`への`atfTempC`/`atfTempValid`追加、`ObdExtFieldId`・`obdEncodeExtFields()`（TLV拡張フィールド、下記参照） | 完了 |
| 3 | `service/obdpoll.cpp` | `obdPoll()`末尾でMode01の29PIDバッチとは別経路の単発UDSリクエストを実行 | 完了 |
| 4 | `service/diddscan.h/.cpp` 新規 | `didScanRun()`（範囲総当たり、NRC 0x22/0x33ヒットのみ記録）・`kDidScanPresets[]`（範囲プリセット） | 完了 |
| 5 | `service/menu.cpp` | ルートに `"OBD"` サブメニュー追加、`"DID Scan"` からプリセット選択→実行→結果一覧 | 完了 |
| 6 | `device/ble_peripheral.cpp` | `notifyObd()`をコア構造体+TLV拡張領域の連結送信に変更 | 完了 |
| 7 | `mobile/lib/models/obd_reading.dart`/`obd_metric.dart` | TLV拡張領域パース対応、`ObdMetric.atfTempC`追加 | 完了 |

**設計判断:**

- `atfTempC`/`atfTempValid`は当初`ObdBlePacket`（コア構造体）の末尾に追加していたが、それだと
  今後PID/DIDを1つ足すたびにアプリ側`ObdReading.fromBytes()`の固定オフセットが全部ズレて壊れる
  （実際に発生した）。そのため`ObdBlePacket`からは外し、コア構造体の直後に連結する
  TLV拡張フィールド領域（`obdEncodeExtFields()`）に移した。「BLE Notify送信」節参照。
- Mode22はMode01の`kPids[]`多PIDバッチ機構（`obdParseMultiResponse()`前提）と応答ヘッダ形式・
  アドレッシング方式が異なるため、送受信・デコードとも意図的に別経路にした（Mode01側は無変更）。
- DIDスキャンは全域`0x0000`〜`0xFFFF`だと応答なしDIDのタイムアウト待ちが支配的で数十分規模になるため、
  通常のCONTINUOUSポーリングには組み込まず、OLEDメニューから手動実行する一時的な調査機能として独立させた
  （`didScanRun()`はcanInit()済み前提、呼び出し元がcanInit/canDeinitのライフサイクルを管理する）。
  導入当初は範囲プリセット選択メニューを設けていたが、全域スキャンを実車で完走させて
  `0x2000-0x2FFF`/`0xE000-0xEFFF`/`0xF000-0xFFFF`にヒット（他は0件）と判明した後、
  プリセット選択自体が不要になったため2026-08-20に廃止し「OBD > DID Scan」選択で
  即座に全域一括スキャンする形に簡略化した（下記「DIDスキャン結果」参照）。
- スキャン中の中断可否: `didScanRun()`は1件ごとに`shouldAbort()`コールバックを呼ぶ設計にし、
  `menu.cpp`側でボタン監視とOLED進捗表示を兼ねさせた（BTN1長押しで中断可能）。

**DID Values機能（実装済み・2026-08-20追加）**: `didReadCandidateValues()`（`service/diddscan.h/.cpp`）が
`kDidCandidates[]`（全域スキャンでOK確認済みの9DID固定リスト）を1件ずつ`22XXYY`で問い合わせ、
`62 DID_HI DID_LO`ヘッダを剥がしたペイロード（実値部分）だけをシリアルログ（`[DIDVal]`タグ）と
OLEDに出す。OLEDメニュー「OBD > DID Values」から実行（ボタン操作は「DID Scan」と同じ配置：
BTN0でヒット送り、BTN1長押しで戻る）。9件のみで数百ms程度のため中断機能は無し。
燃料補給前後・冷間/暖機後に本機能で値を読み、変化するDIDを絞り込む運用は未着手。

読み取り直前に`[DIDVal] 開始 ts=<epoch秒> (<ISO8601>)`をログ出力する（2026-08-20追加）。
`ts`はAWSへアップロードされるOBDデータ（mobile側`obd_uploader.dart`が送る`ts`=`time(nullptr)`の秒値）
と同じ形式のため、この時刻を軸にAWS側のOBDデータ・前回スキャンからの走行データと突き合わせられる。
`time(nullptr)`未同期時（起動直後などでNTP/LTE時刻同期前）は`ts=未同期 millis=...`にフォールバックする
（`log_storage.cpp`と同じ2020-01-01閾値判定）。

**CONTINUOUSモードの1秒間隔ポーリングにも統合（2026-08-20追加）**: PA/SA停車時のスナップショットだけ
では相関の解像度が粗いため（前節「AWSデータとの突き合わせ」参照）、`service/obdpoll.cpp`の`obdPoll()`
末尾（ATF DID `0x2201`単発クエリの直後）で`didReadCandidateValues()`を毎ティック呼ぶようにした。
`pio device monitor -f log2file`をつないだまま走行すれば、停車時の点ではなく走行中の連続データが
`[DIDVal]`ログとして残る。結果（`DidValueResult`）はローカル変数のみでBLE Notify・AWSアップロードの
パイプラインには送らない（調査目的のログ出力専用、`OBDReading`にもフィールド追加していない）。
既存のMode01バッチ+ATFクエリは実測17〜22ms程度（1秒枠に対して余裕あり）で、9DID追加分は正常時
数十ms程度の増加に収まる見込み（応答なし時のワーストケースでも9×50ms=450msで1秒枠に収まる）。

**未実施（実車確認が必要）:**

- `kDidCandidates[]`9件のうち`0xE602`(RPM)/`0xE600`(ECU電圧mV、2026-08-24確定)/残り7件(フラグ/ID系)は
  すべて意味が判明済み。燃料残量・油温は見つかっておらず、必要ならNRC33系6件（セキュリティアクセス
  要）または別ECUへのアクセスを検討する段階

### DID `0x2201`（ATF油温）が実車で一度も成功していない問題（2026-08-24）

**実測結果**: 収集済み全ログファイルで`[OBD] ATF(0x2201)`の応答内訳を集計すると、**応答なし495件・
送信失敗102件・成功（valid=1）0件**。一度も成功していない。「実装済み・実車未テスト」という
従来の記載は正確だが、実態は「テストした結果、全部失敗している」。

**原因の仮説**: `canSendObdRequestUds()`（`device/can.cpp`）は常に固定アドレス`0x18DA0EF1`
（**対象ECU=`0x0E`固定＝エンジンECU**）で送信している。DIDスキャン（`didScanRun()`）も同じ関数を
使っているため、**これまでの全域スキャン（9候補DID＋NRC33系6件）もエンジンECU（`0x0E`）しか
探索していない**。ATF（正確にはCVTフルード）温度はエンジンECUではなくトランスミッション制御
ユニットが持つデータの可能性が高く、もしそうならエンジンECUにいくら`0x2201`を聞いても
「そもそも持っていないので応答なし」になるのは当然という説明がつく。

**ECUアドレススキャン機能を追加（`canScanEcuAddresses()`、`device/can.h/.cpp`）**: 機能アドレッシング
（`0x18DB33F1`）でUDS `DiagnosticSessionControl(10 01)`をブロードキャストし、応答してきた
ECUアドレス（応答ID`0x18DAF1xx`の下位バイト）を収集する。OLEDメニュー「OBD > ECU Scan」から
実行できる（実車確認は未実施）。

**HondaReflashTool（[bouletmarc/HondaReflashTool](https://github.com/bouletmarc/HondaReflashTool)）
にECUアドレス⇔モジュールの対応表を発見**: `GForm_PlatformSelect.cs`にコメントアウトされた形で
残っていた（北米/欧州向けHonda車の一般的なマッピングで、N-VAN固有の裏付けではない点に注意）:

```csharp
if (headers2[0] == 0x0b) AdditionnalCanInfos = " (Shift by wire)";                      //->54008-XXX-XXXX files
if (headers2[0] == 0x0e) AdditionnalCanInfos = " (CVT Transmission (maybe?))";          //->
if (headers2[0] == 0x10) AdditionnalCanInfos = " (ECM with Manual Transmission)";       //->37805-XXX-XXXX files
if (headers2[0] == 0x11) AdditionnalCanInfos = " (ECM with Automatics Transmission)";   //->37805-XXX-XXXX files
if (headers2[0] == 0x1e) AdditionnalCanInfos = " (TCM - Transmission Control Module)";  //->28101-XXX-XXXX files
if (headers2[0] == 0x28) AdditionnalCanInfos = " (VSA Module)";                         //->57114-XXX-XXXX files
if (headers2[0] == 0x2b) AdditionnalCanInfos = " (Electric Brake Booster Module)";      //->39494-XXX-XXXX files
if (headers2[0] == 0x30) AdditionnalCanInfos = " (Electric Power Sterring Module)";     //->39990-XXX-XXXX files
if (headers2[0] == 0x3a) AdditionnalCanInfos = " (Unknown Module)";                     //->39390-XXX-XXXX files
if (headers2[0] == 0x53) AdditionnalCanInfos = " (SRS Module)";                         //->77959-XXX-XXXX files
if (headers2[0] == 0x60) AdditionnalCanInfos = " (Odometer Module)";                    //->78109-XXX-XXXX files
if (headers2[0] == 0x61) AdditionnalCanInfos = " (HUD Module)";                         //->78209-XXX-XXXX files
if (headers2[0] == 0xb0) AdditionnalCanInfos = " (FWD Radar Module)";                   //->36802-XXX-XXXX files
if (headers2[0] == 0xb5) AdditionnalCanInfos = " (FWD Camera Module)";                  //->36161-XXX-XXXX files
if (headers2[0] == 0xef) AdditionnalCanInfos = " (Gateway Module)";                     //->38897-XXX-XXXX files
```

**`0x1E` = TCM（Transmission Control Module、部品番号`28101-XXX-XXXX`）**が有力候補。同リポジトリの
`ECUS_KEYS.txt`（4749行、ROM ID×フラッシュ書き込み用パラメータの一覧）にも部品番号プレフィックス
`28101`が実在しており、2つの独立した情報源が一致した。

一方で`0x0E`には「CVT Transmission (**maybe?**)」という注記があり、これまでの前提（`0x0E`＝
エンジンECU、実測でrpm/throttle等のMode01エンジンデータが正常取得できている）と食い違う。
確信度は低い（"maybe?"付き）注記のため、エンジン+CVTが統合ECU（PCM）になっている可能性、または
単なる開発者コメントの誤りの可能性、両方が考えられ、現時点では判断保留。

**ECUスキャン実車結果（2026-08-24、`logs/device-monitor-260824-220505.log`）**: 走行中（アイドリング
〜1500-2200rpm）に「OBD > ECU Scan」を2回実行、いずれも**`0x0E`以外は応答ゼロ**（`0x1E`含め、
機能アドレッシングに反応するECUは他に見つからなかった）。ただしUDSサービスは機能アドレッシングに
応答しないECU実装も多いため（「プロトコル確認事項」節参照）、これだけで`0x1E`（TCM）の不在を
断定はできない。

**同じログでATF(0x2201)の「応答なし」の正体が判明**: 7回中7回すべてで、`can.cpp`側のISO-TPログに
`[CAN] ISO-TP: 否定応答受信 SID=0x22 NRC=0x31`が対になって出ていた（例:
`device-monitor-260824-220505.log`の198-199行目）。これは**タイムアウトではなく、エンジンECU
（`0x0E`）が明示的に「NRC 0x31 = DID非サポート」と否定応答していた**ということ。ただし
`service/obdpoll.cpp`の呼び出し側（[obdpoll.cpp:200](obdpoll.cpp#L200)）が`ObdRecvResult::NegativeResponse`
と`Timeout`を区別せず両方「応答なし」に丸めてログしていたため、これまでの集計（応答なし495件・
送信失敗102件）ではこの区別が失われていた。今回のケースに限れば「通信断・配線問題」ではなく
「`0x0E`が本当にこのDIDを持っていない」ことがログレベルで裏付けられたことになり、`0x1E`など
別ECUへのアクセスを試す動機が強まった（ログ判別の改修は対応済み、下記参照）。

**`0x1E`宛の物理アドレッシング直叩きを実装（2026-08-24）**: `canSendObdRequestUds()`を宛先
パラメータ化（`ecuAddr`引数追加、既定値`0x0E`で既存呼び出しは変更なし。`can.h/.cpp`）し、
OLEDメニュー「OBD > ATF@0x1E」を追加（`service/menu.cpp`）。ECU `0x1E`宛に`0x2201`を単発で
物理アドレッシング問い合わせし、正常応答(データhex表示)/否定応答(NRC値表示)/無応答のいずれかを
その場でOLED表示する。OLED表示のみでシリアルログに出力していなかったため、後日
`[MENU] ATF@0x1E: ...`のログ出力を追加（`service/menu.cpp`、開始/送信失敗/否定応答(NRC)/
正常応答(hexデータ)/無応答の全パターンをログ化）。

**実車確認結果（2026-08-25、`logs/device-monitor-260825-005146.log`・`-005548.log`）**:
2回とも

```text
[MENU] ATF@0x1E: 開始
[MENU] ATF@0x1E: 応答なし
```

**`0x1E`は物理アドレッシングでの単発`0x2201`問い合わせに一度も応答しなかった**（否定応答NRC
すら返らない、完全な沈黙）。直後のCAN状態ログは`TEC=0 REC=0 busErr=0`と健全で、バス側の通信
エラーが原因ではない。同じような単発問い合わせ（セッション確立なしでのいきなり`0x22`送信）に
`0x0E`は普通に応答している実績と対比すると、`0x1E`はこのN-VANのOBD-IIバス上に存在しない
（または少なくともこのアドレスでは応答しない）可能性が高い。

**結論の更新**: 「`0x0E`には"CVT Transmission (maybe?)"という注記があり、エンジン+CVTが統合ECU
（PCM）になっている可能性」という仮説の方が有力になった。ATF/CVT油温は別アドレスのECUにある
のではなく、そもそもこの車では公開されていない（または`0x0E`自身の未解明DIDに隠れている）
という見方に傾く。次に試すなら`didScanRun()`に`ecuAddr`パラメータ化を追加して`0x1E`以外の
物理アドレス（`0x10`エンジン+MT、`0x11`エンジン+AT等、HondaReflashToolの対応表参照）を
虱潰しに当たるか、NRC33系6件のセキュリティアクセス対応に進むかの判断になる（優先度は未定）。

### DID Values 実測記録（進行中）

複数回読んで値の変化を突き合わせ、燃料残量・油温DIDを絞り込む。

| 日時/ログ | 条件 | `0x2341` | `0x2342` | `0x2601` | `0x2630` | `0xE5FF` | `0xE600` | `0xE602` | `0xF100` | `0xF806` |
|---|---|---|---|---|---|---|---|---|---|---|
| 2026-08-20 19:05 `logs/device-monitor-260820-190531.log` | エンジン始動済み・アイドル(rpm≈950)・燃料9/10・水温87-88°C（Mode01 0x67も同時に87°C確認済み） | `01 00 00` | `00 00 00` | `01 00 00` | `00 00 00` | `00` | `37 C8`(14280) | `0E C4 00` | `00 00 00 00` | `AB 94 54 6C` |
| 2026-08-20 19:15 `logs/device-monitor-260820-191537.log` | 1回目から**約10分間ずっとアイドリング継続**（Mode01ログ無し、走行・停止なし） | `01 00 00` | `00 00 00` | `01 00 00` | `00 00 00` | `00` | `37 D4`(14292) | `0E EC 00` | `00 00 00 00` | `AB 94 54 6C` |
| 2026-08-20 10:26:23Z(ts=1787221583) `logs/device-monitor-260820-192300.log` | 2回目からさらに**約7-8分間ずっとアイドリング継続**（Mode01同時ログ: rpm≈950-960・水温87°C・load≈40%で安定） | `01 00 00` | `00 00 00` | `01 00 00` | `00 00 00` | `00` | `37 AF`(14255) **↓減少** | `0E EC 00`（不変） | `00 00 00 00` | `AB 94 54 6C` |
| 2026-08-20 10:48:41Z(ts=1787222921) `logs/device-monitor-260820-194543.log` | 3回目から22分18秒後。**羽生PA→佐野SA（東北道下り、約17.4km、上り線での計測値だが下りもほぼ同距離と推定）を走行**。燃料計**9/10→8/10へ実際に減少**（水温は87°Cのまま） | `01 00 00` | `00 00 00` | `01 00 00` | `00 00 00` | `00` | `37 70`(14192) **↓減少** | `0F 24 00` | `00 00 00 00` | `AB 94 54 6C` |
| 2026-08-20 11:59:19Z(ts=1787227159) `logs/device-monitor-260820-205624.log` | 4回目から約1時間11分後。**佐野SA→上河内SA（東北道下り、走行距離未確認）を走行**、現在地上河内SA | `01 00 00` | `00 00 00` | `01 00 00` | `00 00 00` | `00` | `37 1E`(14110) **↓減少** | `0F 30 00` | `00 00 00 00` | `AB 94 54 6C` |

**所見（5サンプル後。燃料が動いた区間を2回観測）:**

- `0x2341`/`0x2342`/`0x2601`/`0x2630`/`0xE5FF`/`0xF100`/`0xF806`の7件は、燃料が2回にわたって
  実際に減った区間を含めても**一貫して不変**。この7件は燃料残量DIDから除外して確定でよい。
- `0xE600`は`14280→14292→14255→14192→14110`。最初の小さな上振れを除けば一貫して減少傾向で、
  走行を挟むたびに下げ幅が拡大（羽生→佐野: -63、佐野→上河内: -82）。この時点では燃料残量候補
  として有力視していたが、**AWSデータとの定量的な突き合わせで否定的な結果が出た**
  （詳細は下記「AWSデータとの突き合わせ」参照）。
- `0xE602`は16bit実効部で`0EC4(3780)→0EEC(3820,+40)→0EEC(3820,+0)→0F24(3876,+56)→0F30(3888,+12)`。
  今回（佐野→上河内、約1時間11分）は前回（羽生→佐野、22分）より移動時間・距離が長いはずなのに
  増分は+12と前回の+56より小さく、時間・距離の単純な累積カウンタという見立てとは合わない。
  燃料消費量の累積という見立てだと、この区間で燃料が減った量に対して伸びが鈍いのも気になる点。
  性質はまだ掴めていない。
- 水温は5回とも87°Cで変化しておらず、油温候補としての切り分けはまだできていない。次は**始動直後
  （水温が低い状態）**で読み、水温と連動する値が上記に紛れていないか確認する。

**AWSデータとの突き合わせ（2026-08-20実施）**: AWS側（`obd_data`テーブル、Athena経由。
`aws athena start-query-execution` + `iot_monitor`データベース + `iot-monitor`ワークグループでクエリ）
にはMode01由来の`fuel_rate_lph`・GPS座標が1秒間隔で溜まっている。これを使い、5回のDID Values
サンプル間の**実燃料消費量**（`fuel_rate_lph`の台形積分）と**実走行距離**（GPS座標のhaversine
距離の累積）を計算し、`0xE600`/`0xE602`の変化と定量的に突き合わせた。

| 区間 | 経過時間 | 積分燃料消費量 | GPS実走行距離 | `0xE600`差分 | 消費量あたりの差分 | `0xE602`差分(16bit) |
|---|---|---|---|---|---|---|
| 1→2 | 10.1分 | 0.089L | 0.01km（駐車中） | +12 | +134.8/L | +40 |
| 2→3 | 10.8分 | 0.115L | 0.01km（駐車中） | -37 | -321.7/L | 0 |
| 3→4 | 22.3分 | 1.593L | 16.98km（羽生→佐野、みんカラ等の目安17.4kmとほぼ一致） | -63 | -39.5/L | +56 |
| 4→5 | 70.6分 | 4.450L | 52.29km（佐野→上河内） | -82 | -18.4/L | +12 |

GPS実走行距離が外部の目安値とほぼ一致しており、AWS側データの信頼性自体は確認できた。その上で
`0xE600`の「燃料消費量あたりの差分」は区間ごとに符号・大きさとも一貫しない（+134.8, -321.7,
-39.5, -18.4）。特に3→4・4→5はどちらも実走行区間なのに比率が2倍以上異なり、燃料残量を線形に
反映しているなら揃うはずの比率が揃わない。**「燃料残量の最有力候補」という評価は撤回**する。
全体として下がり基調だったのは事実だが、燃料消費と直接連動している証拠としては弱く、
何らかのノイズを持つ別の瞬時値である可能性の方が高い。`0xE602`も同様に距離との対応が
不整合（4→5が最長距離なのに差分は3→4より小さい）。燃料残量に該当するDIDは、この9候補の
中には無い可能性も含めて再検討が必要。

### 走行中連続データでの検証（2026-08-20、CONTINUOUSモード統合後）

`obdPoll()`への統合後、`logs/device-monitor-260820-220448.log`で約13分間（761秒、347ティック、
約2.2秒間隔）の走行中連続データを取得できた（羽生PA区間再訪、SA区間の発進〜巡航〜停止を含む）。
PA/SA停車時の点サンプルと違い、1トリップ内の連続変化が初めて見える。

**`0xE602` = RPM×4であることが判明・確定**。347サンプル中346件で`0xE602÷4`と`rpm`の差が±30rpm
以内（99.1%）、平均誤差-0.59rpm・標準偏差7.85rpm。数rpm程度のズレはMode01のRPM問い合わせと
Mode22 DIDの問い合わせが同一ティック内でも数十ms離れているために生じる自然な変動で、本質的には
同一の値（RPM）を指している。**`0xE602`は燃料残量・油温のいずれでもなく、RPMの別表現に過ぎない
と結論する**（それまでの「距離連動の累積値」という見立ては誤りだった）。

**`0x2341`/`0x2342`/`0x2601`/`0x2630`/`0xE5FF`/`0xF100`/`0xF806`の7件は、時速0〜107km/h・
rpm最大5948・水温62→87°Cの温間過程を含む実走行中も一切変化なし**。フラグ/ID系という結論を確定する。

**`0xE600`は「燃料残量の有力候補」という評価がさらに崩れた**。生データの挙動:

```text
 t(s) speed rpm   fuel  E600
   0     0  1273   0.6  12030   停車再開直後
   9     0  1168   1.4  14072   rpmが下がるにつれ急上昇
  20     0  1132   1.1  14255   停車中のままプラトーに到達
  68    10  1483   1.4  14335   発進
 120    94  3177   5.1  14330  }
 300   100  3300   8.0  14330  } 巡航中はrpm/speedが大きく振れても
 500   100  3300   6.0  14350  } 14200〜14390の狭い帯でほぼ一定
 712    83  2535   5.1  14292  }
 732    11  1281   1.3  14242   減速開始
 748     0   345   0.4  12775   停止直前
 752     0   950   0.6  12250   停止、再び低い値へ
```

停車直後のアイドリング安定化過程（rpmが1273→970付近へ落ち着く間）で12000台→14300台へ急上昇し、
走行中はrpm(2400〜5948)・speed(63〜107km/h)が大きく振れても14200〜14390の狭い帯にとどまり、
停止に向けて再び12000台へ落ちる。全区間でのPearson相関係数は`rpm`との相関+0.425、`speed`との
相関+0.481、`fuel_rate_lph`との相関+0.251、`coolant`との相関+0.326といずれも弱く、単純な線形の
連続センサー値としては説明できない。燃料残量ならこれほど急激（十数秒で+15〜20%相当）には
動かないはずで、油温なら緩やかに単調増加するはずのため、**どちらの仮説とも整合しない**。
「走行中/停止中」の2値的な状態＋数十秒の遷移時間を持つ何か、という以上の性質は現時点で特定できて
いない。今回のトリップでは停止→発進・走行→停止の遷移がそれぞれ1回ずつしか含まれないため、
再現性の確認にはさらにサンプルが必要。

**シフトポジション操作ログとの照合、および上位/下位バイト分解で「ギア位置」仮説が浮上**:
このログ取得中、実際のシフト操作は「ログ開始時P→即D」「しばらく走行後、停止前にL」「ログ終了
直前にP」という順序だったとユーザーから申告あり。これと`0xE600`の挙動を照合すると:

- t=0（P）: `E600=12030`
- t=0〜24（P→D遷移、rpmが1273→970付近へ収束）: `E600`が`12030`→`14255`へ上昇
- t=24〜712（D巡航）: `14200`〜`14390`の狭い帯でほぼ一定
- t=732〜752（減速→L→P）: `14242`→`12775`→`12250`と降下、Pでの初期値`12030`に近い水準へ戻る

さらに**上位バイトと下位バイトを分けると傾向がよりクリア**になる:

| 区間 | 上位バイト |
|---|---|
| P→D遷移中（t=4〜11） | `0x2E`→`0x2F`→`0x31`→`0x33`（46→47→49→51、段階的に上昇） |
| D巡航中（t=24〜712） | `0x37`〜`0x38`（55〜56）でほぼ完全固定 |
| 減速〜停止（t=736〜765） | `0x37`→`0x32`→`0x31`→`0x2F`→`0x33`→`0x37`と乱れながら降下・再上昇 |

下位バイトはD巡航中も`0x06`〜`0xFF`まで大きくばらつき、速度・rpmとの明確な対応もない（ノイズに近い）。

**ただしビット単位で見ると「ギア位置スイッチの生値」という解釈には疑問符がつく。** D巡航中の
約700秒間、上位バイトは`0x37`(`00110111`)と`0x38`(`00111000`)の間をずっと行き来しているだけで、
これは1ビットのフラグ反転ではなく単なる「55と56の間の±1揺らぎ」（複数ビットが同時に変わる
算術的な+1）。さらに、申告されたシフト操作は「P→即D」という**瞬間的な**操作だったのに対し、
上位バイトは`0x2E→0x2F→0x31→0x33→0x36→0x37`（46→47→49→51→54→55）と**約20秒かけて段階的に**
上昇している（t=4〜24）。物理的なシフトスイッチの生値なら操作の瞬間に1〜2ティックで切り替わる
はずで、20秒かけて滑らかに収束する動きとは整合しない。

**タイミングはギア操作と連動しているが、値そのものはギア位置スイッチの生値ではなく、シフト後に
何らかの物理量が再収束していく過程を反映したアナログ的な値（例: トルクコンバーターのロックアップ
状態・ライン圧・アイドル学習値など、Dで巡航が安定すると一定値に落ち着き、加減速で乱れるもの）
の可能性の方が高い。**「ギア位置そのもの」と「ギア操作をきっかけに変化する別の量」を今のデータ
だけでは区別できていない。

**CVT比（速度/rpm比）・アクセル開度との相関は無し（否定的結果）**: 走行区間（speed>5km/h）
312サンプルで、速度/rpm比（CVT比の代理指標、`speed÷rpm×1000`）・スロットル開度・速度・rpmと
`0xE600`（全体値・上位バイト・下位バイトそれぞれ）の相関係数を計算したが、いずれも|r|<0.31と
弱い。速度/rpm比が大きく動く瞬間（急加速・キックダウン相当）をピンポイントで見ても目立った反応は
無く、そもそも今回のトリップでは比が大きく動く瞬間自体が出発直後・停止直前の遷移区間としか
一致していなかった。**`0xE600`は走行中の瞬間的な運転操作（アクセル開度・加減速・CVT比の変化）
には反応せず、これまでに確認できた唯一の反応は「P⇔D等の状態遷移」のみ**という消去法的な結論に
なる。P/R/N/D/L順次シフト検証で「瞬時に切り替わるスイッチ値」か「じわじわ収束する別の量」かを
決着させるのが引き続き次のステップ。

**P/R/N/D/L順次シフト検証を実施 → ギア位置仮説を撤回（2026-08-20）**:
`logs/device-monitor-260820-223858.log`にて、停車したまま安全な場所でP(22:41:12-41:57)→
R(42:00-42:19)→N(42:30-42:58)→D(43:00-43:28)→L(43:30-43:58)→P(44:01-44:16)と切り替えて
記録（JST、いずれも speed=0km/h・rpmは944〜1030の狭い範囲で終始安定）。

結果、**どのギア位置でも`0xE600`上位バイトはほぼ`0x37`(55)で一定**（時々`0x33`/`0x36`/`0x38`へ
数ティックだけノイズ的に振れる程度）で、ギアによる系統的な違いは見られなかった。
**ギア位置仮説は撤回する。**

前回観測した「P→D遷移で12000台→14300台へ約20秒かけて上昇」という挙動は、ギア操作そのものが
原因ではなく、**エンジン始動直後の速いアイドリング（fast idle、rpm≈1273）が通常アイドリング
（rpm≈970）へ収束していく過程**とギア操作のタイミングがたまたま重なっていただけだった可能性が
高い。今回の検証ではエンジンが既に温間・アイドル安定状態（rpm 944〜1030の狭い範囲で終始）
だったため、その収束過程自体が起きておらず、ギアを変えても値が動かなかった、と考えると
両方の観測結果に矛盾なく説明がつく。

同じP/R/N/D/Lテストで**`0x2341`/`0x2342`/`0x2601`/`0x2630`/`0xE5FF`/`0xF100`/`0xF806`の残り7件も
全ギア位置で完全に不変**を確認済み。ギア位置に反応するDIDはこの9候補の中に無いと言える。

**水温との相関も見かけ上のもの（2026-08-20、全ログ横断で再確認）**: 収集済み全ログファイル
（433サンプル）で`0xE600`と`coolant_c`のPearson相関係数を計算すると全体ではr=0.273（弱い正の
相関）だが、**暖機後（水温64°C以上、422サンプル）に絞るとr=0.078まで低下し実質無相関**（冷間側
11サンプルのみでもr=-0.053で無相関）。水温別の平均値を見ても64〜87°Cの範囲でE600平均は
14100〜14350の間で横ばい、水温上昇に伴う増加傾向は無い。全体でのプラス相関は「エンジン始動直後
だけ水温もE600も両方とも低い値から立ち上がる」という時間的な偶然の一致によるもので、水温そのもの
を反映しているわけではないと判断する。**油温候補としても除外。**

**（時点の記録）消去法的結論**: この時点では`0xE600`はギア位置・CVT比・アクセル開度・燃料残量・
油温・水温のいずれとも系統的に対応せず、唯一確認できた反応は「エンジン始動直後のrpm収束（fast
idle→通常アイドル）」のみだった。**この後の調査（下記「`0xE600`の正体確定」参照）でECU電圧である
ことが判明し解決している。**

**結論の更新（2026-08-20時点）**: `0xE602`はRPM、他7件はフラグ/ID系と確定。`0xE600`は当時
唯一残った未解明枠だった（**2026-08-24にECU電圧と判明・解決、下記参照**）。燃料残量・油温に
該当するDIDは今回の9候補の中には存在しない。全域スキャンで発見済みのDIDは尽きているため、
新規候補を増やすには、別ECU（トランスミッション・メーター等）へのアクセス可否の確認、または
セキュリティアクセスが必要なNRC33系6件（`0x2682`/`0xE402`/`0xE40E`/`0xE813`/`0xE81F`/`0xE829`）
への対応を検討する必要がある。

**燃調（STFT/LTFT等）との相関を再検証 → 連続値としての相関は否定、状態遷移との連動は示唆（2026-08-24）**:
`0xE600`が燃調（燃料噴射補正）に似た動きをするのではという仮説を受け、走行中連続データログ
`logs/device-monitor-260820-220448.log`（347サンプル、`[OBD2]`行のstft/ltft/o2b1s2/o2s1/afrと
`[DIDVal]`行のE600を同一ティックで対応付け）でPearson相関を計算した。

| 相関対象 | 全347サンプル r | 走行区間(312サンプル) r |
| --- | --- | --- |
| stft（短期燃調） | +0.029 | +0.188 |
| ltft（長期燃調） | +0.156 | +0.047 |
| o2b1s2v（O2電圧） | -0.105 | -0.032 |
| o2b1s2trim | -0.142 | +0.024 |
| o2s1ratio（ワイドバンド） | -0.053 | +0.035 |
| o2s1v | -0.027 | +0.066 |
| afr（Lambda） | -0.066 | +0.026 |
| （参考）rpm | +0.425 | +0.134 |
| （参考）speed | +0.481 | +0.278 |

燃調系7項目はいずれも\|r\|<0.19で、既存確認済みのrpm/speed/coolant/fuelとの相関（0.25〜0.48）より
明確に弱く、**連続値としての線形相関は否定的**。ラグ付き相関（E600が数秒遅れて追従するか）でも
stftのみlag4〜5（約9〜11秒遅れ）でr=+0.31〜+0.35まで上がるが「弱め〜中程度」止まりで、loadや
absloadはどのラグでもr=0.2〜0.3で横ばい（系統的な遅延構造なし）。

ただし個別イベントを詳しく見ると、**燃調システムの状態遷移タイミングとE600の変動タイミングは
連動している可能性がある**。同ログのt=717〜761（減速→アイドリングストップ→再始動）を抜粋:

```text
t(s) speed rpm    throttle  stft   E600
717    83  2551     27      -3.9   14297
719    77  5440     20       0.0   14330   減速エンブレ開始（CVT変速比操作でrpm上昇、単発ノイズではなく
721    71  5282     20       0.0   14335   5440→5282→5078→4803→4201→2995→1281と複数ティックで
723    63  5078     21       0.0   14305   滑らかに減少しており実挙動と判断）
725    51  4803     20       0.0   14285
727    36  4201     18       0.0   14342
730    21  2995     16       0.0   14292
732    11  1281     16       0.0   14242   DFCO解除（燃料噴射再開）想定タイミング
734     8  1420     16     -10.2   14260
736     9  1286     15      -9.4   14260
738     8  1247     16     -11.7   12962
740     7  1367     17     -14.1   14167
743     7  1192     17     -11.7   14217
748     0   345     14       0.0   12775   アイドリングストップ発動直前
750     0     0      30       0.0   12750   アイドリングストップ（rpm=0、エンストではなくユーザー
                                             申告により自動停止機構と判明）
752     0   950     14       0.0   12250   D→P操作に伴う再始動
755     0   996     14       0.0   12537
757     0   944     16     -10.2   13295
759     0   997     17     -14.8   14335
761     0   969     16     -10.9   14072
```

- t=719〜732の減速エンブレ区間は**stftが0.0%で完全固定**されている。DFCO（減速時燃料カット）中は
  燃料噴射がないためO2フィードバック補正が働かず0%固定になる、という一般的なECU挙動と整合する。
  この間E600も14200〜14390台の通常巡航帯のままでほとんど動いていない。
- t=734（rpmがアイドル付近まで落ちてDFCO解除・燃料噴射再開と想定されるタイミング）以降、stftが
  動き出す（-10.2%）のとほぼ同時にE600も12962〜14335の間で乱れ始める。ただし符号・大きさの対応は
  一貫していない（t=738でstftがさらにマイナスに振れているのにE600は急落、t=740でstftが最も
  マイナスなのにE600はむしろ回復、など）。
- t=748〜761はアイドリングストップ→再始動で、rpm/stft/E600すべてが乱高下している。これは
  「エンジン始動直後のfast idle収束」という既存の唯一確認できた反応（本セクション冒頭の消去法的
  結論）の2回目の実例（1回目は同ログ冒頭t=0〜57）と見なせ、再現性のある材料になる。

上記の時点では「燃料噴射のオン/オフ切り替わりやエンジン再始動といった離散的なイベント・状態遷移に
反応している値」という評価に留まっていたが、直後にECU電圧との相関を確認したことで正体が確定した
（次項）。

### `0xE600`の正体確定: ECU電圧（mV）（2026-08-24）

燃調（STFT/LTFT）との相関を検証する過程で「エンジン始動直後・アイドリングストップ→再始動で
大きく動く」「減速エンブレ〜DFCO中は安定」というE600の挙動パターンが、バッテリー電圧の挙動
（クランキング直後は低く、オルタネータ発電で上昇、アイドリングストップで電圧降下）とよく似て
いることに気づき、既存Mode01 PID `0x42`（`ecuVoltage`）との相関を計算した。

同じログ（`device-monitor-260820-220448.log`、347サンプル）で`0xE600`と`ecuVoltage`（V）を
突き合わせた結果:

- Pearson相関係数: **全体 r=+0.985**、走行区間のみでも r=+0.865
- `E600 / ecuVoltage(V)`の比率: min=974.1 / max=1014.9 / **平均1000.03**

```text
t(s)  ecuV    E600
   0  11.98   12030
  20  14.18   14255
 738  12.96   12962   （減速中の電圧ドロップ）
 748  12.81   12775
 750  12.72   12750   （アイドリングストップ）
 752  12.20   12250
 757  13.10   13295   （再始動後、電圧回復中）
 759  14.15   14335
```

**`0xE600` = ECU電圧（V）× 1000、つまりミリボルト単位のバッテリー電圧と確定する。** 既存PID
`0x42`（`(A×256+B)/1000`でVに変換）と同じ量を、Mode22側は逆に1000倍したmV値で返しているだけ
だった。これでこれまでの全観測が一貫して説明できる:

- エンジン始動直後に大きく変動 → クランキング後の電圧回復過程（rpm収束と同時に起きるため、
  rpmとの相関r=+0.425もこの電圧変動の副産物）
- アイドリングストップで急落・再始動で回復 → バッテリー電圧そのものの挙動
- 減速時のDFCO巡航区間で安定 → 発電電圧が安定しているだけ
- 燃調（stft/ltft）との相関が終始弱い → 別の量なので当然
- ギア位置・CVT比・水温との相関なし → 当然

**9候補DIDのうち未解明枠はこれで無くなった。** `0x2341`/`0x2342`/`0x2601`/`0x2630`/`0xE5FF`/
`0xF100`/`0xF806`はフラグ/ID系、`0xE602`はRPM、`0xE600`はECU電圧(mV)で全件確定。燃料残量・油温に
該当するDIDはこの9候補には存在しない（`0x2201`のATF油温のみ既知）。今後燃料残量・油温を探すには、
セキュリティアクセスが必要なNRC33系6件、または別ECUへのアクセスを検討する必要がある。

**ネット上の公開情報は期待薄と判断（2026-08-20調査）**: 北米Honda車種フォーラム（Odyssey/Pilot/Ridgeline等）・
GitHub（opendbc等）・みんカラ等を調べたが、今回の9候補DID自体への言及は無し。ATF油温DID`0x2201`の
デコード式（`AA-40`℃、`obdDecodeAtfTemp()`実装済み）だけは北米Honda ATコンピュータの情報と一致し
裏取りできたが、N-VAN(JJ1/JJ2)固有のDIDはJDM専用のケイバンでリバースエンジニアリング事例が
見当たらず、公開情報での特定は見込み薄。**実測による切り分けが主手段**という結論。

**再調査（2026-08-20、より深く）**: N-BOX（同じS07Bエンジン搭載のJDM軽自動車）関連記事、GitHub
`awesome-automotive-can-id`（Honda項目はCivic 8世代のみ）、ECUリバースエンジニアリング技術資料
（トヨタ アクア対象で車種違い）、opendbc等のDBC系リポジトリ、Car Scanner ELM OBD2のカスタムPID
共有コミュニティ、ISO 14229-1標準規格のDID範囲定義を追加で調査。今回の9候補DID自体への直接的な
言及は依然として見つからず。ただし副産物として、**ISO 14229-1の標準仕様で`0xF100`〜`0xF17F`が
「識別情報（Vehicle Manufacturer Specific識別用、VIN`0xF190`に近い領域）」と定義されている**ことを
確認でき、`0xF100`が4回とも`00 00 00 00`で不変という実測結果と整合する（センサー値ではなく識別・
設定系フィールドという見立てを規格側からも裏付け）。`0xE600`/`0xE602`/`0x2341`系のような
メーカー完全独自のDIDについては、公開情報は引き続き見当たらない。**実測による切り分けが唯一の
現実的な手段という結論は変わらず**。

**他車種の解析結果との横展開比較（2026-08-20）**: N-VAN固有の情報は見つからなかったが、
他のHonda車種の解析結果から**設計パターンの傾向**は2つ拾えた（具体的なDID値の一致ではなく、
Hondaの診断PID設計思想としての裏付け）:

1. **GitHub `kerpz/ArduinoHondaOBD`**（旧世代Honda車のMode 21対応、Mode 22とは別の古い診断
   プロトコルだが同じHonda設計思想の系譜）の`torque/HOBD.csv`に、1バイトの中に複数のスイッチ/
   状態フラグをビット単位で詰め込む例が多数ある（例: `Starter Switch,2108,{B:0}` `Aircon
   Switch,2108,{B:1}` `Brake Switch,2108,{B:3}`）。うちの`0x2341`/`0x2342`/`0x2601`/`0x2630`/
   `0xE5FF`が軒並み小さい整数値（`01 00 00`/`00 00 00`）しか返さないのは、この「フラグ/状態系」
   という既存の見立てとよく符合する（具体的に何のフラグかは特定できないが傾向としての裏付け）。
   ※Mode 21はISO14230(KWP2000)系の古いプロトコルでMode 22(UDS)とは別物だが、Hondaが診断PIDに
   ビットフラグを詰め込む設計を世代を超えて使っている点は参考になる。
2. **Honda CR-Vディーゼル（欧州仕様）のDID `0x2512`**は、1つの応答内に複数の独立した値を
   異なるバイトオフセットで詰め込んでいる（`DPF距離: INT16(V:W)/10 [km]`、`DPF燃料消費:
   INT16(X:Y)/100 [l]`、`DPF経過時間: INT16(Z:AA)*60 [s]`、`EGT: O*4 [°C]`）。既存実装の
   `0x2201`（ATF、`domain/obd.cpp`の`obdDecodeAtfTemp()`）や`0x67`（Sensor1/Sensor2）と同じ
   「1DID=複数サブフィールド」構造。`0xE602`の3バイト目が常に`00`なのも、未実装/非搭載の
   2つ目のサブフィールドという可能性を補強する（現状の「16bit実効部+常時0の3バイト目」という
   見立てと整合）。

**残りのサンプリング計画**:

1. ~~走行を挟んで燃料残量が動く区間~~ → **実施済み**（羽生PA→佐野SA、`0xE600`が最有力候補に浮上）
2. **始動直後（水温が低い状態）** → 未実施。水温と連動する値がここまでの候補に紛れていないか確認する
3. 給油直後（燃料だけジャンプ、水温は無関係に推移）→ 未実施。`0xE600`が燃料残量なら給油で増加するはず

### DIDスキャン結果（実車、進行中）

`pio device monitor -f log2file` でログを保存し、`[DIDScan]` 行から集計。写真のOLED表示（180度回転しており誤読しやすい）ではなくログファイルを正とする。

| プリセット範囲 | スキャン状況 | ヒットDID（正常応答=OK） | ヒットDID（NRC、要セキュリティアクセス） |
| --- | --- | --- | --- |
| `0x2000-0x2FFF` | 完了（ログ確認済み、`logs/device-monitor-260820-183943.log`） | `0x2341`, `0x2342`, `0x2601`, `0x2630` | `0x2682`（NRC 0x33） |
| `0x0000-0x0FFF` | 完了（ヒット0件、OLED「Hits(0)」確認のみ・ログ未取得） | — | — |
| `0x1000-0x1FFF` | 完了（ヒット0件、OLED「Hits(0)」確認のみ・ログ未取得。`Coolant cand 0x11xx`＝`0x1100-0x11FF`もこの範囲に包含） | — | — |
| `0x3000-0x3FFF` | 完了（ヒット0件、OLED「Hits(0)」確認のみ・ログ未取得） | — | — |
| `0x4000-0x4FFF` | 完了（ヒット0件、OLED「Hits(0)」確認のみ・ログ未取得。`Coolant cand 0x40xx`＝`0x4000-0x40FF`もこの範囲に包含） | — | — |
| `0x5000-0x5FFF` | 完了（ヒット0件、OLED「Hits(0)」確認のみ・ログ未取得） | — | — |
| `0x6000-0x6FFF` | 完了（ヒット0件、OLED「Hits(0)」確認のみ・ログ未取得） | — | — |
| `0x7000-0x7FFF` | 完了（ヒット0件、OLED「Hits(0)」確認のみ・ログ未取得） | — | — |
| `0x8000-0x8FFF` | 完了（ヒット0件、OLED「Hits(0)」確認のみ・ログ未取得） | — | — |
| `0x9000-0x9FFF` | 完了（ヒット0件、OLED「Hits(0)」確認のみ・ログ未取得） | — | — |
| `0xA000-0xAFFF` | 完了（ヒット0件、OLED「Hits(0)」確認のみ・ログ未取得） | — | — |
| `0xB000-0xBFFF` | 完了（ヒット0件、OLED「Hits(0)」確認のみ・ログ未取得） | — | — |
| `0xC000-0xCFFF` | 完了（ヒット0件、OLED「Hits(0)」確認のみ・ログ未取得） | — | — |
| `0xD000-0xDFFF` | 完了（ヒット0件、OLED「Hits(0)」確認のみ・ログ未取得） | — | — |
| `0xE000-0xEFFF` | 完了（ログ確認済み、`logs/device-monitor-260820-183943.log`） | `0xE5FF`, `0xE600`, `0xE602` | `0xE402`, `0xE40E`, `0xE813`, `0xE81F`, `0xE829`（いずれもNRC 0x33） |
| `0xF000-0xFFFF` | 完了（ログ確認済み、`logs/device-monitor-260820-183943.log`） | `0xF100`, `0xF806` | — |

**全域スキャン完了（`0x0000`〜`0xFFFF`）。ヒットDID総数: OK 9件 / NRC33 6件。** OK系9件（`0x2341`,`0x2342`,`0x2601`,`0x2630`,`0xE5FF`,`0xE600`,`0xE602`,`0xF100`,`0xF806`）が実値取得の対象候補。NRC33系6件は認証（Security Access）が必要なため後回し。

**OK系9件の正体（2026-08-24時点で全件確定）**: `0xE602`=RPM、`0xE600`=ECU電圧(mV)、残り7件
（`0x2341`/`0x2342`/`0x2601`/`0x2630`/`0xE5FF`/`0xF100`/`0xF806`）はフラグ/ID系で走行中も不変。
詳細は「DID Values 実測記録」節の「`0xE600`の正体確定」参照。

`ATF near 0x22xx`（`0x2200-0x22FF`）は`0x2000-0x2FFF`に包含済みのためスキャン不要。

**NRC33系6件のセキュリティアクセス（SID 0x27）公開情報調査（2026-08-24）**: NRC33系6件
（`0x2682`/`0xE402`/`0xE40E`/`0xE813`/`0xE81F`/`0xE829`）はUDSのSecurityAccess（Service 0x27、
Seed-Key方式）を通さないと読めない。DIDスキャンの時と同程度の広さでシード→キー変換アルゴリズム
自体の公開情報を調査したところ、**計算式そのものは見つかった**（下記）が、実際に使う係数値は
未取得という状態。

**Honda用アルゴリズムの計算式が公開されている（[jglim/UnlockECU](https://github.com/jglim/UnlockECU)
の`HondaAlgo1.cs`）**:

```csharp
// Level 1, used for firmware flashing
// Keys are embedded as a 12-byte ascii block in the *.rwd.gz firmware files
key = (seed * k1_mul);
if (k2_mod != 0) key %= k2_mod;
key ^= (k0_xor + seed);
key &= 0xFFFF;
```

16bit演算で `key = ((seed × k1) mod k2) XOR (k0 + seed)`。ただし2つ制約がある:

1. コメントの通り**これは"Level 1"＝ファームウェア書き込み用**のアルゴリズム。係数`k0`/`k1`/`k2`
   は車種・ECUごとに異なり、**その車のファームウェアファイル（`.rwd.gz`）自体に埋め込まれている**
   （抽出には[rwd-xray](https://github.com/jpancotti/rwd-xray)というツールを使う想定、
   `HondaReflashTool`にキー生成の詳細が文書化されているとコメントにあり）。今回のNRC33系6件は
   診断読み取り用のDIDで、Honda ECUの一般的な設計だと書き込み用とは別のセキュリティレベル
   （Level 3等）を要求している可能性が高い。実際どのレベルを要求しているかは、`didScanRun()`が
   受け取ったNRC33応答の中身（要求レベル番号）を確認すれば分かる。
2. リポジトリ内を検索した限り、**`HondaAlgo1`以外のHonda用アルゴリズム（Level2以降）は
   実装されていない**。

その他の収穫:

- **Honda Insight/CR-Z のMCM（モーター制御ユニット、ハイブリッド系）で実際にリバースエンジニアリング
  された実例がある**（[insightcentral.net「CAN Hacking Cryptographic Conundrums」](https://www.insightcentral.net/threads/can-hacking-cryptographic-conundrums.127315/)）。
  HDS（純正診断機）とのCAN通信をキャプチャして解析した結果、**2バイトのマスターキーとチャレンジ
  （シード）をXORする単純な方式**だったという記述が検索結果の要約に見つかった（記事本文はペイ
  ウォールで直接確認できず、要約止まり）。ただしこれはハイブリッド用MCMであり、`HondaAlgo1`とも
  一致しない別の実装の可能性がある。
- [bouletmarc/HondaReflashTool](https://github.com/bouletmarc/HondaReflashTool)（`HondaAlgo1`の
  キー生成が文書化されているとされる。詳細未確認）
- [aeaphichart/HondaECU-1](https://github.com/aeaphichart/HondaECU-1)（K-lineプロトコルのバイク
  向けツールでUDSではなく対象外）
- N-VAN/N-BOXでの言及（日本語コミュニティ含む）は無し

**結論**: 計算式の型（乗算・剰余・XORの組み合わせ）は判明したが、①係数がN-VAN固有で未取得、
②今回必要なセキュリティレベルがLevel1と同じ保証がない、という2つのギャップが残る。次の一手は
`didScanRun()`のNRC33応答から要求レベル番号を確認すること。係数の取得にはN-VANのファームウェア
ファイル(.rwd.gz)の入手＋`rwd-xray`での解析が必要になりそうで、そこまでやる価値があるかは
別途判断（今回のプロジェクトスコープでは優先度低）。

---

## リスクと対処

| リスク | 対処 |
|--------|------|
| 冷却水温が 0x05 非対応 | 0x67 Sensor1 で代替取得可能（確認済み） |
| MAF が 0x10 非対応 | 0x66 で代替取得可能（確認済み、1.69 g/s@idle） |
| 燃費計算（0x5E 非対応） | 0x66 MAF から推算: `mafGs / (14.7×λ×0.745) × 3.6` L/h |
| CAN 未接続・IGN OFF 時のタイムアウト | 全 PID × 100ms = 最大 1秒（許容範囲。正常時は数十msで応答するためもっと短い） |
| バスオフ状態 | 軽量リカバリ（`twai_initiate_recovery()`）→ 連続失敗20回でフル再init |
| GU0/GU1 の配線混同 | GU0=CAN（GPIO4/5/6）、GU1=LTE（GPIO7/8/9、`device/lte.cpp` 使用中）。触るのはGU0のみ |
