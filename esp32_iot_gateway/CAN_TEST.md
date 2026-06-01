# MCP2562FD CAN トランシーバ 試験メモ

ブレッドボード上での単体試験用ドキュメント。  
`blank` env は他の env と接続機器が大きく異なるため独立して管理する。

---

## 背景

GPIO4 / GPIO5 / GPIO6 は通常 LTE モジュール（U128 SIM7080G）に接続するピンだが、  
`blank` env ではこれらを CAN トランシーバのテスト用として流用する。

| GPIO | 通常 env（LTE） | blank env（CAN テスト） |
|------|----------------|------------------------|
| GPIO4 | LTE RX（← U128 TXD） | CAN RX（← MCP2562FD RXD） |
| GPIO5 | LTE TX（→ U128 RXD） | CAN TX（→ MCP2562FD TXD） |
| GPIO6 | LTE_EN（AO3401A パワースイッチ） | CAN_EN（同パワースイッチ流用） |

---

## ハードウェア構成（ブレッドボード）

### 使用部品

- Microchip MCP2562FD（CAN FD トランシーバ、8ピン SOIC / PDIP）
- AO3401A（P チャンネル MOSFET、電源スイッチ）※基板上に実装済み

### ピン接続

```
ESP32-S3          MCP2562FD
─────────────────────────────────────────
GPIO5 (TXD)   →  Pin 1 (TXD)
GND           →  Pin 2 (VSS)
5V            →  Pin 3 (VDD)  ※AO3401A 経由
GPIO4 (RXD)   ←  Pin 4 (RXD)
3.3V          →  Pin 5 (VIO)  ※3.3V ロジックレベル指定
Pin 6 (CANL)     → オシロ CH2 / CAN バスへ
Pin 7 (CANH)     → オシロ CH1 / CAN バスへ
GND (直結)    →  Pin 8 (STBY) ※常に通常動作モード
GPIO6 (EN)    →  AO3401A ゲート（HIGH = 電源 ON）
```

### 電源スイッチ

GPIO6 HIGH → AO3401A ON → MCP2562FD VDD に 5V 供給  
GPIO6 LOW  → AO3401A OFF → MCP2562FD 電源断

### STBY ピンについて

STBY を GND に直結しているため、電源が入った時点で常に通常動作モード。  
ESP32 からの制御は不要。

### VIO について

VIO = 3.3V により TXD / RXD / STBY のロジックレベルが 3.3V になる。  
ESP32（3.3V ロジック）と直結可能。VIO を 5V にすると GPIO を壊すので注意。

---

## ソフトウェア構成

### ビルド環境

| 項目 | 内容 |
|------|------|
| env | `blank` |
| ソースファイル | `src/blank.cpp` のみ（他ファイルは除外） |
| フレームワーク | Arduino + ESP-IDF TWAI ドライバ |
| 追加ライブラリ | なし（TWAI は ESP-IDF 内蔵） |

### blank.cpp の動作

1. GPIO6 HIGH → モジュール電源 ON → 50ms 待機
2. TWAI コントローラ初期化（125kbps、`TWAI_MODE_NO_ACK`）
3. 1秒ごとに id=`0x123`、4バイト（カウンタ値）のフレームを送信
4. 受信フレームがあればシリアルに出力

### `TWAI_MODE_NO_ACK` について

通常の CAN は他ノードからの ACK が必要。1ノード単体では ACK が返らず送信エラーになる。  
`NO_ACK` モードでは ACK なしでも送信成功扱いになるため、単体試験に使用している。  
他ノードと通信する場合は `TWAI_MODE_NORMAL` に切り替える（blank.cpp 冒頭の define を変更）。

### ビット数を 125kbps にしている理由

DS203（ハンディオシロ、帯域 ≈ 1MHz）での観察に適した速度。  
500kbps ではビット波形が丸まって見づらい。  
本番統合時は用途に応じて変更する（500kbps が一般的）。

---

## ビルド・書き込み手順

```powershell
cd esp32_iot_gateway

# ビルドのみ
~/.platformio/penv/Scripts/pio.exe run -e blank

# ビルド＋書き込み
~/.platformio/penv/Scripts/pio.exe run -e blank -t upload

# シリアルモニタ（115200 bps）
~/.platformio/penv/Scripts/pio.exe device monitor
```

---

## シリアル出力の確認

正常時は以下が 1 秒ごとに出力される：

```
=== MCP2562FD CAN test ===
TWAI ready. 125kbps
TX ok  id=0x123 cnt=0
TX ok  id=0x123 cnt=1
TX ok  id=0x123 cnt=2
...
```

`TX fail` が出る場合は TWAI の状態コードとエラーカウンタが続けて表示される。

---

## DS203 によるオシロ確認

### プローブの当て方

| チャンネル | 接続先 |
|-----------|--------|
| CH1 | CANH |
| CH2 | CANL（余裕があれば） |
| GND クリップ | MCP2562FD VSS（GND） |

### 期待される電圧レベル

| 状態 | CANH | CANL | 差動（CANH − CANL） |
|------|------|------|---------------------|
| リセッシブ（アイドル） | ≈ 2.5V | ≈ 2.5V | ≈ 0V |
| ドミナント（データ） | ≈ 3.5V | ≈ 1.5V | ≈ 2.0V |

### 手順

**ステップ 1：フレームの存在確認**

- 時間軸：200ms/div
- 電圧軸：1V/div（オフセット 2.5V 付近）
- トリガ：CH1 立ち上がり、レベル 2.8V 付近
- 1 秒ごとに短いバースト（約 700μs のひと固まり）が見えれば送信できている

**ステップ 2：ビット波形の確認**

- 時間軸：50μs/div
- CAN フレームのビットパターン（単純な交互ではなく id・データに対応した波形）が見える
- CANH と CANL が逆向きに動いていれば差動出力が正常

**ステップ 3：個別ビットの確認（オプション）**

- 時間軸：5〜10μs/div
- 125kbps では 1 ビット = 8μs
- DS203 の帯域限界に近いため波形は多少丸まるが、H/L の遷移は確認できる

### 判定基準

| 観測結果 | 判定 |
|----------|------|
| CANH / CANL が常に 2.5V 固定 | NG：MCP2562FD が駆動していない |
| 1 秒ごとにバーストあり、電圧が 1.5〜3.5V で変化 | OK |
| CANH と CANL が同じ方向に動く | NG：差動出力の異常 |
| CANH と CANL が逆方向に動く | OK |

---

## 今後の作業（TODO）

- [x] DS203 で CANH/CANL の波形確認（完了）
- [x] フェーズ1 ステップ A: 500kbps 実車接続確認（完了）
- [x] フェーズ1 ステップ B: PID スキャン実行・結果を OBD.md に記録（完了）
- [x] PID 0x60/0x80 スキャン → 0x61〜0xA0 の対応状況確認（完了、OBD.md に記録済み）
- [x] フェーズ1.5: 生 CAN 探索（完了、OBD-II ポートでは生 F-CAN トラフィックは不可と判明）
- [x] 新基板（GPIO TX=7/RX=8/EN=9）での動作確認（完了、2026-06-01）
- [x] 全対応 PID のポーリング確認（完了、blank.cpp pollObd() で実機確認済み）
- [ ] OBD.md のフェーズ2 実装へ

---

## フェーズ1: 実車接続確認

プロトコル詳細は `CAN_REFERENCE.md` を参照。  
統合設計（フェーズ2以降）は `OBD.md` を参照。

### ステップ A: 500kbps LISTEN_ONLY で接続確認

**blank.cpp の変更点:**

```diff
- TWAI_TIMING_CONFIG_125KBITS()
+ TWAI_TIMING_CONFIG_500KBITS()

- TWAI_MODE_NO_ACK
+ TWAI_MODE_LISTEN_ONLY
```

送信ロジック（1秒ごとの TX 部分）は削除し、受信のみ残す。

**接続:**

```
MCP2562FD CANH → OBD-II コネクタ Pin 6 (CAN-H)
MCP2562FD CANL → OBD-II コネクタ Pin 14 (CAN-L)
終端抵抗は追加しない（車体側に実装済み）
```

**確認手順:**
1. blank env でビルド・書き込み
2. シリアルモニタを開く（115200 bps）
3. 車の IGN ON（エンジン未始動）
4. シリアルに CAN フレームが流れれば **ハードウェア OK**

### ステップ B: PID サポートスキャン

ステップ A で接続確認が取れたら、`TWAI_MODE_LISTEN_ONLY` → `TWAI_MODE_NORMAL` に変更し、以下の PID スキャンロジックを実装して setup() 末尾で一度だけ実行する。

**実装する関数:**

```cpp
// PID 0x00/0x20/0x40 を送信し、4バイトビットマスクを取得
// 失敗時は 0 を返す
uint32_t getSupportedPidMask(uint8_t supportPid) {
  twai_message_t tx = {};
  tx.identifier = 0x7DF;
  tx.data_length_code = 8;
  tx.data[0] = 0x02;
  tx.data[1] = 0x01;
  tx.data[2] = supportPid;
  if (twai_transmit(&tx, pdMS_TO_TICKS(10)) != ESP_OK) return 0;

  twai_message_t rx = {};
  unsigned long deadline = millis() + 300;
  while (millis() < deadline) {
    if (twai_receive(&rx, pdMS_TO_TICKS(10)) == ESP_OK && rx.identifier == 0x7E8) {
      return ((uint32_t)rx.data[3] << 24) | ((uint32_t)rx.data[4] << 16)
           | ((uint32_t)rx.data[5] << 8)  | rx.data[6];
    }
  }
  return 0;
}

// ビットマスクから PID がサポートされているか判定
// base: 0x00 → PIDs 0x01-0x20 / 0x20 → 0x21-0x40 / 0x40 → 0x41-0x60
bool isPidSupported(uint32_t mask, uint8_t pid, uint8_t base) {
  uint8_t bit = pid - base - 1;
  return (mask >> (31 - bit)) & 1;
}
```

**スキャン実行（setup() 末尾）:**

```cpp
Serial.println("=== OBD-II PID Scan ===");
uint32_t mask0 = getSupportedPidMask(0x00);
uint32_t mask1 = getSupportedPidMask(0x20);
uint32_t mask2 = getSupportedPidMask(0x40);

// Priority 1
const uint8_t p1[] = {0x04, 0x05, 0x0B, 0x0C, 0x0D, 0x11};
// Priority 2
const uint8_t p2[] = {0x0A, 0x0E, 0x0F, 0x10, 0x2F, 0x33, 0x42, 0x5C, 0x5E};

for (uint8_t pid : p1) {
  bool ok = (pid <= 0x20) ? isPidSupported(mask0, pid, 0x00)
                           : isPidSupported(mask1, pid, 0x20);
  Serial.printf("[0x%02X] %s\n", pid, ok ? "OK" : "--");
  if (ok) { /* リクエスト送信 → 応答受信 → 値をデコードして出力 */ }
}
// Priority 2 も同様
```

**期待されるシリアル出力（IGN ON、エンジン未始動）:**

```
=== OBD-II PID Scan ===
[0x04] Engine Load:   OK -> 0%
[0x05] Coolant Temp:  OK -> 85 degC
[0x0B] MAP:           OK -> 97 kPa (boost: -4 kPa)
[0x0C] RPM:           OK -> 0 rpm
[0x0D] Speed:         OK -> 0 km/h
[0x11] Throttle:      OK -> 0%
[0x0F] Intake Temp:   OK -> 32 degC
[0x10] MAF:           OK -> 0.00 g/s
[0x2F] Fuel Level:    OK -> 75%
[0x42] ECU Voltage:   OK -> 12.4 V
[0x5C] Oil Temp:      OK -> 68 degC
[0x5E] Fuel Rate:     -- (not supported)
```

スキャン結果は `OBD.md` の「実車スキャン結果」セクションに記録する。

---

## フェーズ1 実施結果（2026-05-23 完了）

### 判明した重要事項

**1. 29ビット拡張アドレッシングが必須**

Honda N-VAN は 11ビット 0x7DF に応答しない。29ビット SAE J1939 形式が必要。

```
リクエスト: CAN ID = 0x18DB33F1 (extd=1)
応答:       CAN ID = 0x18DAF10E (extd=1)  ← ECU アドレス = 0x0E
```

**2. IGN ON が必須**

電源 OFF / ロック状態では CAN ゲートウェイが起きない。  
`TX OK` が返っても応答なし、bus_err が数秒で数万件に達する。  
IGN ON（エンジン未始動）で安定通信可能。

**3. blank.cpp の現状設定**

```cpp
#define CAN_MODE TWAI_MODE_NORMAL
#define RUN_PID_SCAN

// TX: tx.identifier = 0x18DB33F1; tx.extd = 1;
// RX: 0x18DAF1xx (29-bit) および 0x7E8 (11-bit) の両方を受け入れ
```

**4. スキャン結果サマリ**

| 結果 | PID 一覧 |
|------|---------|
| ✓ 対応 | 0x04 Engine Load, 0x0B MAP, 0x0C RPM, 0x0D Speed, 0x0E Ignition Timing, 0x11 Throttle, 0x33 Baro, 0x42 ECU Voltage |
| ✗ 非対応 | 0x05 Coolant, 0x0A Fuel Pressure, 0x0F Intake Temp, 0x10 MAF, 0x2F Fuel Level, 0x5C Oil Temp, 0x5E Fuel Rate |

詳細は `OBD.md` 参照。
