# OBD2 / Honda N-VAN 調査メモ（背景資料・Mode22探索ガイド）

Honda N-VAN (JJ1/JJ2, 2018年7月〜) 向けOBD2実装の初期調査メモ。**Mode 01の確定PID一覧・対応状況・実装は
`esp32_iot_gateway/CAN_REFERENCE.md` と `esp32_iot_gateway/OBD.md`（実車スキャン結果・実装済みデコーダ）が
正**。本書はそちらに載っていない背景知識（CANバス構成・Mode22独自PIDの調査手法・CANスニッフィング手順・
外部参考リンク）に絞った資料として残す。

---

## N-VAN の CAN バス構成

みんカラ「トレ猫」さんによるリバースエンジニアリング結果:

| バス | 速度 | ID フォーマット | トランシーバ | 用途 |
|------|------|----------------|------------|------|
| **F-CAN** | 500 kbps | 標準 11bit | TJA1051 | エンジン・AT系 |
| **B-CAN** | 125 kbps | 拡張 29bit | TJA1042 | ボディ系（ドア・ライト等） |

OBD2 コネクタからアクセスできるのは基本的に **F-CAN**（実装済みの`device/can.cpp`もF-CAN側を使用）。

## S07B エンジンについて

N-VAN (JJ1/JJ2) と N-BOX (JF3/JF4) は**同じ S07B エンジン**を搭載している。

```
S07B: 660cc 直3 DOHC VTEC ターボ（ターボ車）/ NA（NA車）
```

これにより、**N-BOX JF3/JF4 での OBD2 実績が N-VAN にそのまま転用できる可能性が高い**。
N-BOX コミュニティでの知見も積極的に参考にする。

---

## ホンダ独自 PID（Mode 22）

現状（`OBD.md`確認済み）で燃料残量・油温はMode 01非対応のまま未解決。以下はMode 22探索の手がかり。

### Mode 22 の特性

- OBD2 標準の「サポートPID確認」機構が **Mode 22 には存在しない**
- PID は 2バイト（0x0000〜0xFFFF）で非公開
- 調べる手段: ①コミュニティのリバースエンジニアリング結果 ②総当たりスキャン ③HDS等メーカー純正ツール

### 確認済み PID（他ホンダ車での実証あり）

以下は主に **Honda Civic 10th gen（FC1/FK7/FK8、K20C系エンジン）** で Autel AL519 等と照合して確認されたもの。
S07B エンジンの N-VAN/N-BOX への転用は**未確認だが試す価値あり**。

| PID（Mode 22） | データ | 計算式 | 単位 | 確認状況 |
| -------------- | ------ | ------ | ---- | -------- |
| `0x0167` | 冷却水温（ECT） | B − 40 | °C | Civic FC1/FK8 で実測値と一致確認 |
| `0x0168` | 吸気温（IAT、インタークーラー後） | B − 40 | °C | Civic FC1/FK8 で実測値と一致確認 |
| `0x0168` | 吸気温2（IAT2、インタークーラー前） | C − 40 | °C | 同上（同PID、別バイト） |
| `0x2201` | ATF/CVT油温 | A − 40 | °C | 複数ホンダ車で確認 |

> **補足:** `0x0167` / `0x0168` は Mode 22 ではなく **Mode 01 拡張（0x67/0x68）** として解釈される場合もある（ツールによって表記が異なる）。実車では実際に Mode 01 拡張 PID 0x67/0x68 として応答することを確認済み（`OBD.md`参照）。

### 未確認・候補 PID

| PID（Mode 22） | 推定データ | 計算式（推定） | 根拠 |
| -------------- | ---------- | -------------- | ---- |
| `0x1101` | 冷却水温 | A − 40 (°C) | 旧型ホンダ車での報告あり |
| `0x4001` | 冷却水温（別候補） | 要検証 | 一部フォーラムでの言及 |

### 商用 OBD2 製品の実績（ブースト圧取得確認）

| 製品 | 対応モデル | 取得できるデータ | PID |
| ---- | ---------- | ---------------- | --- |
| BLITZ OBD2-BR1A | 現行ホンダ全般 | 水温・ブースト | **非公開** |
| PIVOT Dual Gauge RS | N-BOX JF3/JF4 S07B(T/C) | 水温・ブースト | **非公開** |
| PIVOT GT GAUGE-60 | N-VAN JJ1/JJ2 S07B(T/C) | 水温 | **非公開** |

これらが動作している = データは取得可能だが、使用 PID は製品の仕様として非公開。
（水温は既に0x67で代替取得できているため、残る有力ターゲットは燃料残量・油温）

### Mode 22 リクエスト/レスポンス例

```
リクエスト（冷却水温 PID 0x0167）:
03 22 01 67 00 00 00 00
│  │  └──┘ PID 0x0167
│  └─ Mode 22
└─ データ長 3バイト

レスポンス:
05 62 01 67 AA BB 00 00
│  │  └──┘  └─┘
│  │  PID   データバイト
│  └─ 0x62 = Mode22 + 0x40
└─ データ長 5バイト

計算: B − 40 = BB − 40 (°C)
```

---

## N-VAN 固有の既知の制限

- 標準 OBD2 PID 0x05（冷却水温）が返ってこないという報告が複数（実車確認済み。0x67で代替取得済み）
- PIVOT の N-VAN 対応 OBD2 水温計は製品として存在する → 独自 PID で取っている可能性大
- N-VAN 専用の完全な PID リスト・DBC ファイルは **現時点で非公開・未公開**

---

## CAN バスの物理的仕組み

### バス型トポロジー

CAN は全ノードが同じ2線（CAN H / CAN L）に並列接続されている。
バスに乗った信号は**全ノードが同時に受信する**。

```
エンジンECU ──┬── CAN H ──┬── AT-ECU ──┬── ABS-ECU ──┬── OBD2コネクタ
              │            │             │              │
             終端          │            終端      ESP32+トランシーバ
             抵抗        CAN L          抵抗
```

### LISTEN_ONLY が成立する理由

CAN フレームには **ACK スロット**がある。送信側がここを Recessive(1) にして、受信した誰かが Dominant(0) に引き下げることで「受け取った」を伝える。

```
通常モード:    ECU 送信 → ESP32 受信 → ESP32 が ACK 返す
LISTEN_ONLY: ECU 送信 → ESP32 受信 → ACK 返さない（黙って受信のみ）
```

ACK を返すノードが**他に1台でもいれば**バスは正常に動く。
ESP32 が黙っていても他の ECU が ACK を返すので、車両の動作には一切影響しない。

### 「メーターに映るデータは傍受できる」の原則

ECU はリクエストとは無関係に、制御のために CAN バスへ定期的にデータを**送り続けている**。
メーターに表示するためにはエンジン ECU がメーター ECU へデータを送る必要があり、
そのフレームが ESP32 にも届く。

```
エンジンECU ──── CAN バス ──── メーターECU
                    │
                  ESP32 ← 横取り（傍受）
```

**メーターに表示されているデータは必ず CAN バスを流れている。**
逆に、メーターに表示されないデータ（ブースト圧等）は流れていない可能性もあるが、
ECU が内部制御のために他の ECU へ送っていれば傍受できる。

---

## CAN スニッフィング

**注記**: `OBD.md`の「フェーズ1.5: 生CAN探索」で、OBD-IIポート経由では生F-CANトラフィックが
一切観測できないことが実車確認済み（診断リクエストへの応答フレームのみ通る）。以下の手法は
OBD-IIポートではなく**F-CANバス配線へ直接タップする**場合にのみ有効な参考情報として残す。

### OBD2 リクエストとの違い

| | OBD2 リクエスト（アクティブ） | スニッフィング（パッシブ） |
|--|-------------------------------|--------------------------|
| 方向 | プル型（聞きに行く） | プッシュ型（流れてくる） |
| ECU への影響 | リクエスト負荷あり | 一切なし |
| 取得頻度 | リクエスト間隔に依存 | ECU の送信間隔（数ms〜数十ms） |
| 取得できるデータ | OBD2 として公開されているもの | ECU が使っている全データ |
| 解析難易度 | 低（PID → 値の対応が明確） | 高（バイト位置・計算式を自分で特定） |

両者は**同じバス上で同時に動作可能**。TWAI_MODE_NORMAL に戻せばリクエストを送りながらスニッフィングもできる。

### ESP32 (TWAI) での実装

```cpp
#include "driver/twai.h"

twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(
    TX_PIN, RX_PIN,
    TWAI_MODE_LISTEN_ONLY  // バスに影響を与えない
);
twai_timing_config_t  t_config = TWAI_TIMING_CONFIG_500KBITS(); // F-CAN
twai_filter_config_t  f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

twai_driver_install(&g_config, &t_config, &f_config);
twai_start();

twai_message_t msg;
while (true) {
    if (twai_receive(&msg, pdMS_TO_TICKS(100)) == ESP_OK) {
        printf("%lu %03X", millis(), msg.identifier);
        for (int i = 0; i < msg.data_length_code; i++)
            printf(" %02X", msg.data[i]);
        printf("\n");
    }
}
```

SavvyCAN 等の PC ツールで読む場合は SLCAN フォーマットで出力する:

```cpp
// SLCAN フォーマット: t[ID][DLC][DATA]\r
printf("t%03X%d", msg.identifier, msg.data_length_code);
for (int i = 0; i < msg.data_length_code; i++) printf("%02X", msg.data[i]);
printf("\r");
```

### キャプチャデータの解析手順

**Step 1: フレーム種別を把握する**

```
ID      送信間隔  用途の見当
0x1xx    10ms    高頻度 → 回転数・スロットル・ブースト系
0x2xx    10ms    高頻度 → 車速・ブレーキ系
0x4xx   100ms    低頻度 → 水温・油温系（変化が遅い）
```

送信間隔が短い = リアルタイム制御データ（回転数・車速・ブースト等）
送信間隔が長い = ゆっくり変化するデータ（水温・外気温等）

**Step 2: 変化するバイトを特定する**

同じ ID のフレームを時系列で並べて差分を見る:

```
時刻   ID    B0  B1  B2  B3  B4  B5  B6  B7
1000  0x158  00  00  1A  F8  00  00  00  00
1010  0x158  00  00  1B  04  00  00  00  00
1020  0x158  00  00  1B  10  00  00  00  00
                        ↑↑↑ B2, B3 が変化している
```

**Step 3: 既知データとの照合**

標準 OBD2 で取った値と突き合わせて意味を特定する:

```
Mode01 0x0C で RPM = 1726rpm 取得済み
→ OBD2 標準の式: (A×256 + B) / 4

0x158 の B2B3 = 0x1AF8
→ (0x1A×256 + 0xF8) / 4 = 1726rpm ← 一致！
→ 0x158 の B2〜B3 = エンジン回転数と確定
```

**Step 4: 状態変化との相関で特定する**

| 操作 | 変化するバイト = 候補 |
|------|----------------------|
| アクセルを踏む | 回転数・MAP・ブースト・トルク |
| 暖機（水温上昇） | 水温・油温 |
| 走行 | 車速・ホイールスピード |
| ブレーキ | ブレーキ圧 |
| エアコン ON/OFF | フラグビット |
| クルーズコントロール ON/OFF | フラグビット（1ビット単位で変化） |

### F-CAN / B-CAN とデータの所在

```
F-CAN (500kbps) → パワートレイン系
  ・エンジン回転数・車速・スロットル
  ・ブースト圧（ECU 間制御に使用している可能性）
  ・クルーズ制御状態（制御結果）
  ・水温・吸気温

B-CAN (125kbps) → ボディ系
  ・ドア開閉・ライト・ウィンカー
  ・クルーズコントロールスイッチ入力 ← スイッチの生信号はこちら
  ・エアコン設定

OBD2 コネクタから直接見えるのは F-CAN のみ。
B-CAN を見るには専用の接続が別途必要。
```

クルーズコントロールの場合、スイッチ信号（B-CAN）より「制御中フラグ」（F-CAN）の方が取りやすい。フラグは1ビットの変化なので比較的特定しやすい。

### 解析ツール

| ツール | 用途 |
|--------|------|
| **SavvyCAN**（無料） | グラフ表示・差分フィルタ・DBC編集 |
| **Python + pandas** | CSV ログの統計・相関分析 |
| **Wireshark + SocketCAN** | Linux 環境での詳細解析 |

---

## Mode 22 PID 探索方法論

### NRC（ネガティブレスポンスコード）を区別する

総当たりスキャン時に応答の有無だけでなく **NRC を区別**することで候補を絞れる:

```
応答パターン:
  05 62 XX XX AA BB ...  → 正常（データあり）
  03 7F 22 31 ...        → NRC 0x31: requestOutOfRange（PID 存在しない）
  03 7F 22 22 ...        → NRC 0x22: conditionsNotCorrect（存在するが今は読めない）
  03 7F 22 33 ...        → NRC 0x33: securityAccessDenied（認証が必要）
  タイムアウト           → ECU がこのモードを未実装
```

`0x22` や `0x33` が返ってきた PID は**存在はする**ので特に注目。
（`device/can.h`の`ObdRecvResult::NegativeResponse`は既にNRC取得に対応済み。総当たりスキャンを実装する場合はこの戻り値を使える）

### 公式リファレンスへのアクセス

| 手段 | 内容 | 備考 |
|------|------|------|
| **Honda techinfo** | ディーラー向けサービスマニュアル | 72時間 $10 程度で一般契約可能 |
| **i-HDS / HDS** | Honda 純正診断ソフト | J2534 対応アダプタが別途必要 |
| **Hondata** | K20C 系チューニングツール | 一部 PID が公開されている |

Honda techinfo のサービスマニュアルには「データリスト」項目が記載されており、HDS で表示できる項目名と単位が確認できる（PID 番号そのものは非公開の場合もある）。

---

## Mode 22 調査の進め方（未着手）

```
1. 0x0167（Civic 実証済み）→ 0x1101 → 0x4001 の順で水温系候補を試す
   （水温は既に0x67で代替取得済みのため優先度は低い）

2. 燃料残量・油温はMode22総当たりスキャンが必要
   → NRC 0x22/0x33 が返る PID を重点的に調査

3. 公式情報が欲しければ Honda techinfo を一時契約
```

---

## 参考リンク

### N-VAN 固有

- [CAN通信解析（みんカラ/トレ猫）](https://minkara.carview.co.jp/userid/471038/car/3519160/7764223/note.aspx) — N-VAN JJ1/2 の CAN バス解析その1
- [CAN通信解析2（みんカラ/トレ猫）](https://minkara.carview.co.jp/userid/471038/car/3519160/7764881/note.aspx) — メーターECU コネクタ詳細
- [OBD2スキャナ動作報告（みんカラ/ほんち）](https://minkara.carview.co.jp/userid/270003/car/2951672/8045505/note.aspx) — N-VAN でのスキャナ動作確認
- [N-VAN OBD2 情報掲示板（価格.com）](https://bbs.kakaku.com/bbs/K0001059021/SortID=22101893/) — ユーザー間の PID 情報共有

### Honda 全般

- [Honda カスタム PID（Honda Pilot フォーラム）](https://www.piloteers.org/threads/monitor-honda-custom-obd2-pids-transmission-temp-etc.137202/) — Mode 22 PID の実績値まとめ
- [Honda カスタム PID（Civic フォーラム）](https://www.civinfo.com/threads/custom-pids-share.433458/) — Civic での独自 PID 共有
- [ArduinoHondaOBD（GitHub）](https://github.com/kerpz/ArduinoHondaOBD) — Arduino で Honda OBD を読む実装例

### OBD2 一般

- [commaai/opendbc（GitHub）](https://github.com/commaai/opendbc) — 主要メーカーの DBC ファイル集（N-VAN は未収録）
- [OBD2 PID Overview（CSS Electronics）](https://www.csselectronics.com/pages/obd2-pid-table-on-board-diagnostics-j1979) — PID 一覧・計算式
- [MCP2515 + Raspberry Pi で OBD リクエスト（Qiita）](https://qiita.com/suzutsuki0220/articles/7cfdeb334efa4ffe3070) — CAN + OBD2 実装例（日本語）
