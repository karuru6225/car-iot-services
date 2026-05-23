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

- [ ] DS203 で CANH/CANL の波形確認
- [ ] USB-CAN アダプタを使った双方向通信テスト（`TWAI_MODE_NORMAL` に切り替え）
- [ ] 通信速度・フォーマットの決定（500kbps? CAN FD?）
- [ ] `device/can.h/.cpp` として本番コードに統合
