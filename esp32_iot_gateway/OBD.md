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

## アーキテクチャ統合概要

既存の 3 層構造への追加。依存ルール（device ← domain ← service）を守る。

```
device/can.h/.cpp    新規  TWAI ラッパー（GPIO4=RX, GPIO5=TX, GPIO6=EN, 500kbps）
domain/obd.h/.cpp    新規  OBDReading 構造体・PID デコード関数
domain/telemetry     変更  buildObdPayload() を追加
service/pubqueue     変更  ObdEntry・pushObd() を追加
service/monitor      変更  measure() に CAN ポーリング、publish() に OBD 追加
device/oled          変更  oledShowObdData() を追加（2ページ）
main.cpp             変更  CAN init、OLED ページローテーション
```

---

## device/can.h インターフェース

Honda N-VAN は 29ビット拡張アドレッシングが必須（11ビット 0x7DF は無応答）。

```cpp
#pragma once
#include <driver/twai.h>
#include <stdint.h>

static const uint8_t  CAN_RX_PIN  = 8;
static const uint8_t  CAN_TX_PIN  = 7;
static const uint8_t  CAN_EN_PIN  = 9;
static const uint32_t CAN_REQ_ID  = 0x18DB33F1; // 29-bit functional addressing
static const uint32_t CAN_RESP_MASK = 0x18DAF100; // 応答 ID の上位 24bit (下位8bit = ECU addr)

// GPIO9 HIGH → 50ms 待機 → TWAI 500kbps NORMAL モード起動
bool canInit();

// TWAI 停止 → GPIO9 LOW
void canDeinit();

// Mode 01 PID リクエストを CAN_REQ_ID (29-bit) に送信
bool canSendObdRequest(uint8_t pid);

// 0x18DAF1xx (29-bit) または 0x7E8 (11-bit) からの応答を受信（timeoutMs 内）
bool canReceiveObdResponse(uint8_t *data, uint8_t *dlc, uint32_t timeoutMs = 200);
```

### sendObdRequest の実装ポイント

```cpp
twai_message_t tx = {};
tx.identifier = CAN_REQ_ID;  // 0x18DB33F1
tx.extd = 1;                 // 29-bit 必須
tx.data_length_code = 8;
tx.data[0] = 0x02;           // PCI: Single Frame, length=2
tx.data[1] = 0x01;           // Mode 01
tx.data[2] = pid;
// data[3..7] = 0x00 (ISO 15765-4 パディング)
twai_transmit(&tx, pdMS_TO_TICKS(10));
```

### receiveObdResponse の応答チェック

```cpp
// Honda N-VAN は 0x18DAF10E (ECU addr=0x0E) で応答
// 将来他の ECU に対応する場合を考慮して 0x18DAF1xx 全体を受け入れる
bool is29bit = rx.extd && (rx.identifier & 0xFFFFFF00) == 0x18DAF100;
bool is11bit = !rx.extd && rx.identifier == 0x7E8; // フォールバック
if (is29bit || is11bit) { /* 処理 */ }
```

毎 `measure()` サイクルで `canInit()` / `canDeinit()` を呼ぶ（バスオフ自動リカバリ）。

---

## domain/obd.h データ構造・デコード関数

**実車スキャン結果を反映。非対応 PID（冷却水温・MAF・燃料流量等）は除外。**

```cpp
#pragma once
#include <stdint.h>
#include <time.h>

struct OBDReading {
  // Mode 01 取得可能（確認済み）
  uint16_t rpm;           // 0x0C: (A*256+B)/4 [rpm]
  uint8_t  speed_kmh;    // 0x0D: A [km/h]
  uint8_t  load_pct;     // 0x04: A*100/255 [%]
  uint8_t  map_kpa;      // 0x0B: A [kPa 絶対圧]
  uint8_t  baro_kpa;     // 0x33: A [kPa]
  int8_t   boost_kpa;    // map_kpa - baro_kpa [kPa]
  uint8_t  throttle_pct; // 0x11: A*100/255 [%]
  float    timing_deg;   // 0x0E: A/2.0-64.0 [°BTDC]
  float    ecu_voltage;  // 0x42: (A*256+B)/1000.0 [V]
  // 0x66/0x67 で取得可能（確認済み）
  float    maf_gs;       // 0x66: (B*256+C)/32 [g/s]
  int16_t  coolant_c;   // 0x67 Sensor1: B-40 [°C]（0x05 非対応のため代替）
  float    fuel_rate_lph;// MAF 推算: maf_gs / (14.7×0.745) × 3.6 [L/h]

  bool     valid;
  time_t   ts;
};

// デコード関数
bool obdDecodeRpm(const uint8_t *data, uint8_t dlc, OBDReading &out);
bool obdDecodeSpeed(const uint8_t *data, uint8_t dlc, OBDReading &out);
bool obdDecodeLoad(const uint8_t *data, uint8_t dlc, OBDReading &out);
bool obdDecodeMap(const uint8_t *data, uint8_t dlc, OBDReading &out);
bool obdDecodeBaro(const uint8_t *data, uint8_t dlc, OBDReading &out);
bool obdDecodeThrottle(const uint8_t *data, uint8_t dlc, OBDReading &out);
bool obdDecodeTiming(const uint8_t *data, uint8_t dlc, OBDReading &out);
bool obdDecodeEcuVoltage(const uint8_t *data, uint8_t dlc, OBDReading &out);
// 0x66: sensors=0x01(N-VAN), rate=(B*256+C)/32 g/s
bool obdDecodeMafAlt(const uint8_t *data, uint8_t dlc, OBDReading &out);
// 0x67: A=bitmap(0x03=S1+S2), B=Sensor1 temp (B-40°C)
bool obdDecodeCoolantAlt(const uint8_t *data, uint8_t dlc, OBDReading &out);
```

デコード共通ルール:
- `data[1] != 0x41` または `data[2] != 要求PID` の場合は false を返す
- `dlc` が必要バイト数未満の場合は false を返す

---

## service/pubqueue への追加

### ObdEntry（実車スキャン結果を反映した固定小数点構造体）

```cpp
struct ObdEntry {
  uint16_t rpm;
  uint8_t  speed_kmh;
  uint8_t  load_pct;
  uint8_t  map_kpa;
  uint8_t  baro_kpa;
  int8_t   boost_kpa;
  uint8_t  throttle_pct;
  int8_t   timing_x2;      // °BTDC × 2（0.5° 分解能）
  uint16_t ecu_mv_div10;   // V × 100（0.01V 分解能）
  uint16_t maf_x32;        // g/s × 32（0x66 の raw 値そのまま）
  int8_t   coolant_c;      // 0x67 Sensor1: -40〜127°C → int8 で十分
  uint16_t fuel_rate_x20;  // L/h × 20（0.05 L/h 分解能）
  uint32_t ts;
};  // 合計 ~22 バイト
```

`EntryType::Obd = 3` を追加し、`buildTopic` / `buildPayload` の switch に case を追加する。

---

## service/monitor への追加

### MeasureResult

```cpp
struct MeasureResult {
  SensorReading reading;
  SensorVariant ble[QUEUE_SIZE];
  int bleCount = 0;
  OBDReading obd;  // 追加
};
```

### measure() への CAN ポーリング追加

```cpp
result.obd = {};
result.obd.ts = time(nullptr);

if (canInit()) {
  uint8_t data[8];
  uint8_t dlc;

  struct { uint8_t pid; bool(*decode)(const uint8_t*, uint8_t, OBDReading&); } pids[] = {
    {0x0C, obdDecodeRpm},
    {0x0D, obdDecodeSpeed},
    {0x04, obdDecodeLoad},
    {0x0B, obdDecodeMap},
    {0x33, obdDecodeBaro},
    {0x11, obdDecodeThrottle},
    {0x0E, obdDecodeTiming},
    {0x42, obdDecodeEcuVoltage},
    {0x66, obdDecodeMafAlt},    // 0x10 非対応のため代替
    {0x67, obdDecodeCoolantAlt},// 0x05 非対応のため代替
  };
  for (auto &p : pids) {
    if (canSendObdRequest(p.pid) && canReceiveObdResponse(data, &dlc, 200))
      if (p.decode(data, dlc, result.obd))
        result.obd.valid = true;
  }

  // boost = MAP - Baro
  if (result.obd.baro_kpa > 0)
    result.obd.boost_kpa = (int8_t)(result.obd.map_kpa - result.obd.baro_kpa);
  else
    result.obd.boost_kpa = (int8_t)(result.obd.map_kpa - 101);

  // 燃費推算（MAF から）
  if (result.obd.maf_gs > 0)
    result.obd.fuel_rate_lph = result.obd.maf_gs / (14.7f * 0.745f) * 3.6f;

  canDeinit();
}
```

### publish() への追加

```cpp
// RPM > 0（エンジン動作中）のときのみ送信
if (result.obd.valid && result.obd.rpm > 0) {
  queue.pushObd(result.obd);
}
```

---

## AWS ペイロード形式

トピック: `sensors/{device_id}/data`（既存パイプライン流用）

```json
{
  "type": "obd",
  "rpm": 1200,
  "speed": 87,
  "load": 23,
  "map": 97,
  "baro": 101,
  "boost": -4,
  "throttle": 12,
  "timing": 1.5,
  "ecu_v": 14.29,
  "maf": 1.69,
  "coolant": 72,
  "fuel_rate": 0.56,
  "ts": 1746143700
}
```

---

## OLED レイアウト（2ページ）

SSD1306 128×64、TextSize=1（6×8px、最大 21 文字/行）

```
Page 1: 走行・エンジン系
+──────────────────────+
|OBD  1/2              |
|──────────────────────|
|RPM:  949    0 km/h   |
|TPS:  14%  Load: 26%  |
|MAP: 40kPa CLT:  72C  |
|IGN:+1.5  ECU:14.29V  |
+──────────────────────+

Page 2: 燃料系
+──────────────────────+
|OBD  2/2              |
|──────────────────────|
|MAF:  1.69 g/s        |
|FUL:  0.56 L/h [maf]  |
|BST:  -61 kPa         |
|BARO: 101 kPa         |
+──────────────────────+
```

CONTINUOUS モードの表示順:
- バッテリー画面 → 3秒 → OBD Page 1 → 3秒 → OBD Page 2 → 次サイクルまで

---

## 実装順序

各ステップで `pio run -e esp32-s3-devkitc-1-develop` のビルドが通ることを確認してからコミット。

| # | 対象ファイル | 変更内容 | 状態 |
|---|------------|---------|------|
| 1 | `device/can.h/.cpp` 新規 | TWAI ラッパー（29-bit アドレッシング） | 未着手 |
| 2 | `domain/obd.h/.cpp` 新規 | OBDReading 構造体・デコード関数 | 未着手 |
| 3 | `domain/telemetry.h/.cpp` | `buildObdPayload()` 追加 | 未着手 |
| 4 | `service/pubqueue.h/.cpp` | `ObdEntry`、`pushObd()` 追加 | 未着手 |
| 5 | `service/monitor.h/.cpp` | CAN ポーリング・publish 追加 | 未着手 |
| 6 | `device/oled.h/.cpp` + `main.cpp` | OLED 2ページ・ローテーション | 未着手 |

---

## リスクと対処

| リスク | 対処 |
|--------|------|
| 冷却水温が 0x05 非対応 | 0x67 Sensor1 で代替取得可能（確認済み） |
| MAF が 0x10 非対応 | 0x66 で代替取得可能（確認済み、1.69 g/s@idle） |
| 燃費計算（0x5E 非対応） | 0x66 MAF から推算: `maf_gs / (14.7×0.745) × 3.6` L/h |
| CAN 未接続・IGN OFF 時のタイムアウト | canInit() 後に全 PID × 200ms = 最大 1.6秒（許容範囲） |
| エンジン停止中の誤 AWS 送信 | `rpm > 0` チェックで送信をガード |
| バスオフ状態 | canDeinit/canInit サイクルで自動リカバリ |
