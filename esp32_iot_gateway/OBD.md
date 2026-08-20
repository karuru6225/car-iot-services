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

**DID スキャン機能（実装済み）**: `service/diddscan.h/.cpp` に `didScanRun()` を実装。指定範囲を `22XXYY` で
総当たりし、正常応答（`62`）と NRC `0x22`/`0x33`（存在するが今は読めない／認証必要）のみを記録する
（`0x31` 非対応・タイムアウトは件数カウントのみ）。OLED メニュー「OBD > DID Scan」からプリセット範囲
（`0x1000` 刻み 16 分割＋候補領域 3 件、`kDidScanPresets[]`）を選んで実行する。全域を一度に総当たりすると
数十分〜1時間規模になるため範囲を区切っている。スキャン中は BTN1 長押しで中断可能。  
燃料補給前後・冷間/暖機後でデータが変化する DID を絞り込む運用は今後の課題（結果はログ出力のみ、
差分比較の自動化は未実装）。

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
`domain/obd.cpp`）。フィールド順は`OBDReading`と同一、`bool`は`uint8_t`、`time_t`は`uint32_t`
に固定。末尾は`validMask`（uint32_t、`kPids[]`配列順のPIDごとのデコード成否ビットマスク）。
合計95バイト固定（コア構造体。オフセットは`domain/obd.h`のコメント・
`mobile/lib/models/obd_reading.dart`の`ObdReading.fromBytes()`のオフセットと完全一致させること）。

**コア構造体は増減させない**（フィールドを足すたびにアプリ側の固定オフセット読み取りが
全部ズレて壊れるため）。ATF温度（Mode22 DID 0x2201）等、今後も増減しうる値はコア構造体の
直後に連結するTLV拡張フィールド領域に置く（`obdEncodeExtFields()`、`domain/obd.h`の
`ObdExtFieldId`）:

```text
[extCount:1] ([fieldId:1][len:1][data:len]) × extCount
```

アプリ側（`ObdReading.fromBytes()`）は知らない`fieldId`を`len`分読み飛ばすため、ファーム・
アプリいずれかだけを更新しても壊れない（新フィールド追加時、ファーム側は`obdEncodeExtFields()`
に1行足すだけ、アプリ側は`fromBytes()`のswitchに1caseとフィールド追加だけで済む）。

### チャンクフォーマット

```text
[0]     : seq   (uint8, 0-indexed)
[1]     : total (uint8, 総チャンク数)
[2..]   : payload（最大18バイト、コア構造体95バイト+TLV拡張領域を18バイトずつ分割）
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
- DIDスキャンは全域`0x0000`〜`0xFFFF`だと応答なしDIDのタイムアウト待ちが支配的で数十分〜1時間規模に
  なるため、通常のCONTINUOUSポーリングには組み込まず、OLEDメニューから範囲を区切って手動実行する
  一時的な調査機能として独立させた（`didScanRun()`はcanInit()済み前提、呼び出し元がcanInit/canDeinit
  のライフサイクルを管理する）。
- スキャン中の中断可否: `didScanRun()`は1件ごとに`shouldAbort()`コールバックを呼ぶ設計にし、
  `menu.cpp`側でボタン監視とOLED進捗表示を兼ねさせた（BTN1長押しで中断可能）。

**未実施（実車確認が必要）:**

- DID `0x2201`（ATF油温）の実車応答確認（コード上は実装済みだが実測値との突き合わせ未実施）
- 燃料残量・油温DIDの探索そのもの（プリセット範囲を実車で順に試す運用は未着手）

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
