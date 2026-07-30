# OBD-II 統合設計

フェーズ1（実車接続確認・PID スキャン）の結果と、フェーズ2以降の実装設計をまとめる。  
プロトコル詳細は `CAN_REFERENCE.md`、フェーズ1手順・ハードウェア構成は `CAN_TEST.md` を参照。

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
| 0x68 | Charge Air Cooler Temp | !! マスク対応だが応答なし | — |
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

**確定した取得可能データ一覧（フェーズ2 実装対象・新基板 2026-06-01 実機確認済み）:**

**全28PID実装済み**（`domain/obd.h/.cpp`・`service/obdpoll.cpp`、実装差分は本ドキュメント後半の「domain/obd.h データ構造・デコード関数」参照）。

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

燃費推算: `fuel_rate_lph = maf_gs / (14.7 × 0.745) × 3.6`

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
| 0x5E Fuel Rate | 0x66 から推算: `maf_gs/(14.7×0.745)×3.6` | **推算で対応** |
| 0x2F Fuel Level | Mode 22 探索が必要 | 未対応 |
| 0x5C Oil Temp | Mode 22 探索が必要 | 未対応 |

Mode 22（Honda 独自拡張）で燃料残量・油温を取得できる可能性があるが未確認。フェーズ2では Mode 01 で取得できる値のみを実装する。

### Mode 22 実機テスト候補

Mode 22 は UDS（ISO 14229）サービス 0x22（ReadDataByIdentifier）。  
リクエスト: `03 22 [DID_HI] [DID_LO] 00 00 00 00`  
正常レスポンス先頭: `04 62 [DID_HI] [DID_LO] [VALUE]`  
否定レスポンス: `03 7F 22 31`（NRC=0x31 = DID 非サポート）

| DID | データ | デコード式（暫定） | 優先度 | 状態 |
| --- | ------ | ---------------- | ------ | ---- |
| `0x2201` | ATF 温度 | byte27(AA) - 40 [°C] | 高 | **未テスト**（Honda 製 CVT 複数車種で確認済み・N-VAN も同系統の可能性大） |
| 未確定 | 燃料残量 | 不明 | 高 | DID スキャン要 |
| 未確定 | 油温 | 不明 | 中 | DID スキャン要 |

**DID スキャン方針**: `0x0000`〜`0xFFFF` に `22XXYY` を送り、`62` 応答（正常）が返る DID を列挙する。  
燃料補給前後・冷間/暖機後でデータが変化する DID を絞り込む。

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

新しい動作モード `OperationMode::CONTINUOUS_OBD`（メニュー "Continuous OBD" または Shadow
`override_next_mode: "continuous_obd"` から入る）が、既存 CONTINUOUS の仕組み（5分境界
待機ループ・BLE notify）を維持したまま OBD ポーリングを追加する。

```
config.h               変更  OperationMode に CONTINUOUS_OBD を追加
device/can.h/.cpp      新規  TWAI ラッパー（GPIO4=RX, GPIO5=TX, GPIO6=EN, 500kbps）
domain/obd.h/.cpp      新規  OBDReading 構造体・PID デコード関数
service/obdpoll.h/.cpp 新規  全PID逐次ポーリング（canInit()済み前提）
service/shadow.h/.cpp  変更  override_next_mode="continuous_obd" 対応
service/menu.h/.cpp    変更  "Continuous OBD" メニュー項目
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
bool canSendObdRequest(uint8_t pid);
bool canReceiveObdResponse(uint8_t *data, uint8_t *dlc, uint32_t timeoutMs = 100);
```

ピンは `boardPins()` 経由で取得する（ハードコードしない）:
- `CAN_RX_PIN = boardPins().gu00Pin`（GPIO4、MCP2562FD RXD 側）
- `CAN_TX_PIN = boardPins().gu01Pin`（GPIO5、MCP2562FD TXD 側）
- `CAN_EN_PIN = boardPins().gu0EnPin`（GPIO6、AO3401A ゲート）

`CAN_TEST.md` のブレッドボード試験章の配線（GPIO5=TXD, GPIO4=RXD）そのまま。当初ドラフトは
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

## domain/obd.h データ構造・デコード関数（実装済み・全28PID対応）

**実車スキャン結果を反映。非対応 PID（0x05水温・0x10 MAF・0x5E燃料流量等）は除外。**

```cpp
struct OBDReading {
  // 初期実装分（10PID）
  uint16_t rpm;           // 0x0C: (A*256+B)/4 [rpm]
  uint8_t  speed_kmh;     // 0x0D: A [km/h]
  uint8_t  load_pct;      // 0x04: A*100/255 [%]
  uint8_t  map_kpa;       // 0x0B: A [kPa 絶対圧]
  uint8_t  baro_kpa;      // 0x33: A [kPa]
  int8_t   boost_kpa;     // map_kpa - baro_kpa [kPa]（obdpoll.cpp で計算）
  uint8_t  throttle_pct;  // 0x11: A*100/255 [%]
  float    timing_deg;    // 0x0E: A/2.0-64.0 [°BTDC]
  float    ecu_voltage;   // 0x42: (A*256+B)/1000.0 [V]
  float    maf_gs;        // 0x66: (B*256+C)/32 [g/s]（0x10 非対応のため代替）
  int16_t  coolant_c;     // 0x67 Sensor1: B-40 [°C]（0x05 非対応のため代替）
  float    fuel_rate_lph; // MAF 推算: maf_gs / (14.7×0.745) × 3.6 [L/h]（obdpoll.cpp で計算）

  // 追加実装分（18PID・20フィールド。デコード式は「確定した取得可能データ一覧」参照）
  float    stft_pct, ltft_pct;                          // 0x06, 0x07
  float    o2_b1s2_v, o2_b1s2_trim_pct;                 // 0x15
  uint16_t engine_run_time_sec;                         // 0x1F
  uint16_t mil_distance_km;                             // 0x21
  float    o2_s1_ratio, o2_s1_voltage;                  // 0x24
  uint8_t  evap_purge_pct;                              // 0x2E
  uint8_t  warmups_since_cleared;                       // 0x30
  uint16_t distance_since_cleared_km;                   // 0x31
  float    catalyst_temp_c;                             // 0x3C
  float    absolute_load_pct;                           // 0x43
  float    commanded_afr;                               // 0x44
  uint8_t  throttle_b_pct;                              // 0x47
  uint8_t  accel_pedal_d_pct, accel_pedal_e_pct;        // 0x49, 0x4A
  uint8_t  fuel_type;                                   // 0x51
  float    sec_o2_trim_st_pct, sec_o2_trim_lt_pct;      // 0x55, 0x56

  bool     valid;
  time_t   ts;
};
```

デコード関数は28個（PIDごとに1関数、0x15と0x24のみ1関数で2フィールドを埋める）。
device/service に依存しない純粋関数。共通ルール:
`data[1] != 0x41` または `data[2] != 要求PID` または `dlc` が必要バイト数未満なら false
（`obd.cpp` 内 `checkHeader()` ヘルパーで共通化）。

---

## service/obdpoll.h ポーリング関数（実装済み）

```cpp
OBDReading obdPoll(); // 全28PID逐次問い合わせ（canInit()済み前提）
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

`OperationMode::CONTINUOUS_OBD`（`config.h`）は既存の `DEEP_SLEEP`/`CONTINUOUS`/
`ONE_SHOT_CONTINUOUS` と異なり、`ONE_SHOT_CONTINUOUS` のように自動で `DEEP_SLEEP` には
戻らない持続モード。

- `setOperationMode(OperationMode newMode)`（`main.cpp` 内 static 関数）にモード遷移を
  集約し、`CONTINUOUS_OBD` への出入りで `canInit()`/`canDeinit()` を呼ぶ。既存の全ての
  `g_mode = ...` 代入箇所をこの関数呼び出しに置換済み。
- `setup()` 冒頭で行っていた `gu0EnPin` の常時 HIGH 初期化は削除し、`canDeinit()` 呼び出し
  （未 init でも GPIO6 を LOW に確定する）に統合した。
- `runContinuousLoop()` の既存 1秒間隔ブロック（`lastNotify`）に相乗りさせ、
  `CONTINUOUS_OBD` 中は `obdPoll()` → `oledShowObdData()` を呼ぶ。
- 同ループの `oledUpdateCountdown()` は `CONTINUOUS_OBD` 中はスキップ（OBD 画面が
  画面全体を占有するため）。
- BTN1 長押しは `CONTINUOUS_OBD` → `DEEP_SLEEP` のみ（3状態トグルにはしない。
  `CONTINUOUS_OBD` への入口はメニュー/Shadow のみ）。

### モードへの入り方

- OLED メニュー: `"Continuous OBD"` 項目（`service/menu.cpp`、`"Continuous"` の直後）
- AWS Shadow: `override_next_mode: "continuous_obd"`（`service/shadow.cpp`、
  `one_shot_continuous` と同じ delta 経路をテーブル駆動化して対応）

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

**未実装（今回のスコープ外）**: AWS への publish（`domain/telemetry`・`service/pubqueue`
統合）。送信方法は別途検討する。

---

## リスクと対処

| リスク | 対処 |
|--------|------|
| 冷却水温が 0x05 非対応 | 0x67 Sensor1 で代替取得可能（確認済み） |
| MAF が 0x10 非対応 | 0x66 で代替取得可能（確認済み、1.69 g/s@idle） |
| 燃費計算（0x5E 非対応） | 0x66 MAF から推算: `maf_gs / (14.7×0.745) × 3.6` L/h |
| CAN 未接続・IGN OFF 時のタイムアウト | 全 PID × 100ms = 最大 1秒（許容範囲。正常時は数十msで応答するためもっと短い） |
| バスオフ状態 | 軽量リカバリ（`twai_initiate_recovery()`）→ 連続失敗20回でフル再init |
| GU0/GU1 の配線混同 | GU0=CAN（GPIO4/5/6）、GU1=LTE（GPIO7/8/9、`device/lte.cpp` 使用中）。触るのはGU0のみ |
