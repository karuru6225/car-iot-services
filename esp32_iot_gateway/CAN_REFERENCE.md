# CAN / OBD-II 知識リファレンス

ESP32-S3 + MCP2562FD で車載 CAN バスを扱うための参照ドキュメント。  
統合設計は `OBD.md`（フェーズ2以降）を参照。ブレッドボード単体試験の手順は本書末尾「7. ブレッドボード単体試験」参照。

---

## 1. OBD-II / ISO 15765-4 プロトコル基礎

### CAN ID 体系

| CAN ID | 用途 |
|--------|------|
| 0x7DF | Functional Addressing（全 ECU へのブロードキャスト要求） |
| 0x7E0〜0x7E7 | Physical Addressing（ECU 個別指定の要求） |
| 0x7E8〜0x7EF | ECU 応答（0x7E8 = エンジン ECU、0x7E9 = AT ECU、...） |

一般的な OBD-II 診断では 0x7DF でブロードキャストし、0x7E8 の応答を受け取る。

### ISO 15765-4 Single Frame フォーマット

```
送信フレーム (CAN ID=0x7DF, DLC=8):
  Byte 0: PCI  = 0x02 （Single Frame、データ長 2 バイト）
  Byte 1: Mode = 0x01 （Mode 01: 現在データ取得）
  Byte 2: PID
  Byte 3〜7: 0x00 （ISO 15765-4 パディング必須）

受信フレーム (CAN ID=0x7E8, DLC=8):
  Byte 0: PCI  = 0x04 （Single Frame、データ長 4 バイト）
  Byte 1: 0x41 （Mode 01 + 0x40 = 肯定応答）
  Byte 2: PID  （要求した PID をエコーバック）
  Byte 3: A    （値バイト 1）
  Byte 4: B    （値バイト 2、PID によっては不使用）
  Byte 5〜7: 不定
```

上記はCANバス上の生フレーム（ワイヤーフォーマット）。`can.cpp` の `canReceiveObdResponse()` は
Byte 0（PCI）を剥がしたペイロードをアプリ側に返すため、`obd.cpp` のデコード関数群が実際に見るのは
`data[0] == 0x41` かつ `data[1] == 要求PID` であることを確認してから `data[2]`（A）以降を読む形になる。  
`data[0] == 0x7F`（Negative Response Code）の場合は ECU が PID 非対応。

### PID サポートスキャン

Mode 01 の特殊 PID（0x00, 0x20, 0x40, 0x60, ...）を送ると、次の 32 個の PID のサポート状況を 4 バイトのビットマスクで返す。

```
PID 0x00 → data[3〜6] の 32 ビット = PID 0x01〜0x20 のサポート状況
PID 0x20 → PID 0x21〜0x40
PID 0x40 → PID 0x41〜0x60
PID 0x60 → PID 0x61〜0x80
```

ビット配置（MSB が最小 PID）:

```
data[3] bit7 = PID (base+1)  が対応
data[3] bit6 = PID (base+2)  が対応
...
data[6] bit0 = PID (base+32) が対応
```

判定式:
```cpp
uint32_t mask = ((uint32_t)data[3] << 24) | ((uint32_t)data[4] << 16)
              | ((uint32_t)data[5] << 8)  | data[6];
uint8_t  bit  = pid - base - 1;   // base = 0x00/0x20/0x40/0x60
bool supported = (mask >> (31 - bit)) & 1;
```

---

## 2. 車の電源状態と CAN 通信

| 電源状態 | CAN バス状態 | OBD-II 応答 | 実用メモ |
|---------|------------|------------|---------|
| **電源 OFF**（キー抜き） | 無信号（ECU スリープ） | なし | 全タイムアウト → `valid=false` で自動スキップ |
| **ACC**（アクセサリー） | BCM・インフォテイメント等は起動。エンジン ECU は限定的 | 電気系（0x2F 燃料残量, 0x42 ECU電圧）は応答することがある。エンジン系は 0 か無応答 | |
| **IGN ON**（エンジン未始動） | 全 ECU 起動 | ほぼ全 PID に応答（RPM=0, 速度=0） | **PID スキャンに最適**。バスが安定していて全 ECU 起動済み |
| **エンジン始動後** | フル動作 | 全 PID | 実測値が取得可能 |

**実装上の考慮点:**
- 電源 OFF 時: `canSendObdRequest()` が ESP_ERR_TIMEOUT → 即リターン、タイムアウト待ちは発生しない
- RPM(0x0C) = 0 のとき、エンジン系データの AWS 送信をスキップするフィルタを推奨
- ACC 状態での誤送信を防ぐため、RPM > 0 を確認してからフル計測を行う設計が安全

---

## 3. Mode 01 PID 一覧（Honda 対応状況付き）

Honda 対応: ◎=高対応率(80%+) / ○=中程度(50-80%) / △=低対応率(20-50%) / ×=非対応

### Priority 1: 必須（全車共通で高い対応率）

| PID  | 名称            | 応答バイト | デコード式              | 単位   | Honda |
|------|-----------------|-----------|------------------------|--------|-------|
| 0x04 | エンジン負荷    | A         | A × 100 / 255          | %      | ◎     |
| 0x05 | 冷却水温度      | A         | A − 40                 | °C     | ◎     |
| 0x0B | 吸気管絶対圧    | A         | A（boost = A − 大気圧） | kPa   | ○     |
| 0x0C | エンジン RPM    | AB        | (A × 256 + B) / 4      | rpm    | ◎     |
| 0x0D | 車速            | A         | A                      | km/h   | ◎     |
| 0x11 | スロットル開度  | A         | A × 100 / 255          | %      | ◎     |

### Priority 2: 推奨（スキャン結果で確認後に実装）

| PID  | 名称           | 応答バイト | デコード式                | 単位   | Honda |
|------|----------------|-----------|--------------------------|--------|-------|
| 0x0A | 燃料圧力       | A         | A × 3                    | kPa    | ◎     |
| 0x0E | 点火タイミング | A         | A / 2 − 64               | °BTDC  | △     |
| 0x0F | 吸気温度       | A         | A − 40                   | °C     | ○     |
| 0x10 | MAF            | AB        | (A × 256 + B) / 100      | g/s    | ◎     |
| 0x2F | 燃料残量       | A         | A × 100 / 255            | %      | ◎     |
| 0x33 | 大気圧         | A         | A                        | kPa    | △     |
| 0x42 | ECU 電圧       | AB        | (A × 256 + B) / 1000     | V      | ◎     |
| 0x5C | エンジン油温   | A         | A − 40                   | °C     | ◎     |
| 0x5E | 燃料消費率     | AB        | (A × 256 + B) × 0.05     | L/h    | △     |

※ 0x5E 非対応時は 0x10（MAF）から推算: `mafGs / (14.7 × 1000 × 0.745) × 3600`  
　（理論空燃比 14.7、ガソリン密度 0.745 kg/L）

### Priority 3: 拡張・任意

| PID  | 名称               | 応答バイト | デコード式                       | 単位   | Honda   |
|------|--------------------|-----------|----------------------------------|--------|---------|
| 0x06 | 短期燃料トリム     | A         | (A − 128) × 100 / 128           | %      | ○       |
| 0x07 | 長期燃料トリム     | A         | (A − 128) × 100 / 128           | %      | ○       |
| 0x14 | O2 センサ電圧 B1S1 | AB        | A × 0.005 V / (B−128)×100/128 % | V / %  | ◎       |
| 0x46 | 外気温             | A         | A − 40                          | °C     | △       |
| 0x5B | HV バッテリー残量  | A         | A × 100 / 255                   | %      | ◎（HV） |

### PID スキャン専用（値取得不可）

| PID  | 対象範囲     |
|------|-------------|
| 0x00 | 0x01〜0x20 |
| 0x20 | 0x21〜0x40 |
| 0x40 | 0x41〜0x60 |
| 0x60 | 0x61〜0x80 |

---

## 4. Honda Mode 22 拡張 PID（参考）

Honda は Mode 01 で取得できないパラメータを Mode 22 で提供することがある。  
現フェーズでは実装しないが、将来の拡張候補として記録する。

| 機能                 | Mode | PID   | デコード式    | 単位 |
|----------------------|------|-------|--------------|------|
| トランスミッション油温 | 22   | 0x2201 | A − 40       | °C   |

Mode 22 のリクエストフォーマット:
```
Byte 0: PCI  = 0x03
Byte 1: Mode = 0x22
Byte 2: PID_high
Byte 3: PID_low
Byte 4〜7: 0x00（パディング）
```

---

## 5. OBD-II アダプタ コネクタ配線

OBD-II アダプタから引き出した 16 本の配線を 4ピンコネクタ × 4 に分割して収容する。

### Connector 1 — CAN + 電源（実装済み・常用）

| OBD-II Pin | 信号 |
|-----------|------|
| 16 | +12V |
| 5 | Signal GND |
| 6 | CAN-H |
| 14 | CAN-L |

### Connector 2 — シリアル系（K-Line / J1850）

| OBD-II Pin | 信号 |
|-----------|------|
| 7 | K-Line |
| 15 | L-Line |
| 2 | J1850 Bus+ |
| 10 | J1850 Bus− |

### Connector 3 — メーカー任意①

| OBD-II Pin | 信号 |
|-----------|------|
| 4 | Chassis GND |
| 1 | Manufacturer |
| 3 | Manufacturer |
| 8 | Manufacturer |

### Connector 4 — メーカー任意②

| OBD-II Pin | 信号 |
|-----------|------|
| 9 | Manufacturer |
| 11 | Manufacturer |
| 12 | Manufacturer |
| 13 | Manufacturer |

**備考:**
- Connector 1 のみ接続すれば CAN 通信は動作する
- Chassis GND（Pin 4）は車体アースからも取得可能なため Connector 3 に収容
- アダプタによっては Manufacturer ピン（1/3/8〜13）が未配線の場合がある

---

## 6. 終端抵抗について

OBD-II ポート経由で接続する場合、車体側に既に 120Ω × 2 の終端抵抗が実装されている。  
**追加の終端抵抗を接続してはならない**（並列接続になりバス特性が崩れる）。

ブレッドボード単体テスト（車なし）の場合は、CANH-CANL 間に 120Ω を 1 本接続する必要がある。

---

## 7. ブレッドボード単体試験（MCP2562FD 動作確認・完了済み）

新規基板で CAN トランシーバを追加した際の単体動作確認手順。`blank` env（`src/blank.cpp` のみを
ビルドする独立 env）を使う。ピン配置は現行実装（GU0 = GPIO4/5/6）に合わせて記載する。

### ハードウェア構成

**使用部品**: Microchip MCP2562FD（CAN FD トランシーバ、8ピン SOIC / PDIP）、AO3401A（Pチャンネル
MOSFET、電源スイッチ、基板上に実装済み）

**ピン接続**:

```
ESP32-S3               MCP2562FD
──────────────────────────────────────────
GPIO4 (gu00Pin, TXD) →  Pin 1 (TXD)
GND                  →  Pin 2 (VSS)
5V                   →  Pin 3 (VDD)  ※AO3401A 経由
GPIO5 (gu01Pin, RXD) ←  Pin 4 (RXD)
3.3V                 →  Pin 5 (VIO)  ※3.3V ロジックレベル指定
Pin 6 (CANL)            → オシロ CH2 / CAN バスへ
Pin 7 (CANH)            → オシロ CH1 / CAN バスへ
GND (直結)           →  Pin 8 (STBY) ※常に通常動作モード
GPIO6 (gu0EnPin, EN) →  AO3401A ゲート（HIGH = 電源 ON）
```

GPIO6 HIGH → AO3401A ON → MCP2562FD VDD に 5V 供給。GPIO6 LOW → 電源断。STBY は GND 直結のため
ESP32 からの制御は不要（常に通常動作モード）。VIO=3.3V で TXD/RXD/STBY のロジックレベルが 3.3V
になり ESP32 と直結可能（VIO を 5V にすると GPIO を壊すので注意）。

### ソフトウェア構成

| 項目 | 内容 |
|------|------|
| env | `blank` |
| ソースファイル | `src/blank.cpp` のみ（他ファイルは除外） |
| フレームワーク | Arduino + ESP-IDF TWAI ドライバ |
| 追加ライブラリ | なし（TWAI は ESP-IDF 内蔵） |

**単体試験時の動作**: GPIO6 HIGH → 50ms 待機 → TWAI 初期化（125kbps、`TWAI_MODE_NO_ACK`）→
1秒ごとに id=`0x123` の4バイトフレームを送信 → 受信フレームがあればシリアル出力。

`TWAI_MODE_NO_ACK` を使う理由: 通常の CAN は他ノードからの ACK が必要だが、1ノード単体では ACK
が返らず送信エラーになる。NO_ACK モードなら ACK なしでも送信成功扱いになるため単体試験に使える
（他ノードと通信する本番相当の試験では `TWAI_MODE_NORMAL` に切り替える）。

125kbps にしているのは DS203（ハンディオシロ、帯域 ≈ 1MHz）での観察に適した速度のため（500kbps
ではビット波形が丸まって見づらい）。本番は 500kbps（本書冒頭「1. OBD-II / ISO 15765-4」参照）。

**ビルド・書き込み**:

```powershell
cd esp32_iot_gateway
~/.platformio/penv/Scripts/pio.exe run -e blank              # ビルドのみ
~/.platformio/penv/Scripts/pio.exe run -e blank -t upload    # ビルド＋書き込み
~/.platformio/penv/Scripts/pio.exe device monitor             # シリアルモニタ（115200bps）
```

正常時は `TX ok  id=0x123 cnt=N` が1秒ごとに出力される。`TX fail` の場合は TWAI の状態コードと
エラーカウンタが続けて表示される。

### DS203 オシロでの確認

| チャンネル | 接続先 |
|-----------|--------|
| CH1 | CANH |
| CH2 | CANL（余裕があれば） |
| GND クリップ | MCP2562FD VSS（GND） |

| 状態 | CANH | CANL | 差動（CANH − CANL） |
|------|------|------|---------------------|
| リセッシブ（アイドル） | ≈ 2.5V | ≈ 2.5V | ≈ 0V |
| ドミナント（データ） | ≈ 3.5V | ≈ 1.5V | ≈ 2.0V |

**確認手順**: ①時間軸200ms/div・電圧軸1V/div（オフセット2.5V付近）・CH1立ち上がりトリガでバースト
（約700μsのひと固まり、1秒ごと）を確認 → ②時間軸50μs/divでビットパターンを確認（CANHとCANLが逆
方向に動けば差動出力は正常）→ ③（任意）時間軸5〜10μs/divで個別ビットを確認（125kbpsでは1ビット
=8μs、DS203の帯域限界に近いが遷移は確認できる）。

**判定基準**: CANH/CANLが常に2.5V固定 → NG（MCP2562FDが駆動していない）。1秒ごとにバーストあり
かつ電圧が1.5〜3.5Vで変化 → OK。CANHとCANLが同じ方向に動く → NG（差動出力の異常）。
