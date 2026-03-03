# LTE 実装計画（Unit Cat-M / SIM7080G）

## ハードウェア構成

| 項目 | 内容 |
|---|---|
| LTE モジュール | M5Stack U128（SIM7080G CAT-M/NB-IoT） |
| SIM | SORACOM Cat-M |
| 接続インターフェース | UART（Serial2） |
| M5Atom S3 接続ピン | G1 = RXD2, G2 = TXD2（底面 HY2.0-4P）※ラベルと逆 |
| ライブラリ | TinyGSM (`#define TINY_GSM_MODEM_SIM7080`) |

```
M5Atom S3           U128 (SIM7080G)
G1 (TXD2) ──────── RX
G2 (RXD2) ──────── TX
5V        ──────── VCC
GND       ──────── GND
```

---

## 追加ファイル

```
infra__lte.h / infra__lte.cpp   ← Infrastructure: SIM7080G アダプター
```

既存ファイルの変更：

| ファイル | 変更内容 |
|---|---|
| `config.h` | LTE 定数追加（APN・ピン・タイムアウト） |
| `m5stamp_lite_test01.ino` | `flushQueue()` に `lte.enqueue()` 追加、`lte.flush()` 呼び出し追加 |

---

## `infra__lte` クラス設計

```cpp
class LteModem {
public:
  bool begin();                         // モデム起動・APN 接続
  void enqueue(const SwitchBotData &d); // 内部バッファに追加
  bool flush();                         // バッファ全件を HTTP POST で送信
  void end();                           // 切断・モデムシャットダウン
private:
  SwitchBotData _buf[MAX_TARGETS];
  int           _count = 0;
};
extern LteModem lte;
```

バッファ管理・送信順序・再送ロジックはすべて `LteModem` が責任を持つ。
Application 層はバッファ構造を意識しない。

---

## Application 層の変更（`m5stamp_lite_test01.ino`）

### `flushQueue()` の変更

```cpp
static void flushQueue()
{
  SwitchBotData d;
  view.clear();
  while (xQueueReceive(scanner.queue, &d, 0) == pdTRUE)
  {
    view.sensorData(d);
    lte.enqueue(d);    // LTE バッファに追加（LTE 未使用時は no-op）
  }
}
```

### Deep Sleep モードの `loop()` 変更

```cpp
if (!gNormalMode)
{
  scanner.start(SCAN_TIME);
  scanner.clearResults();
  flushQueue();          // 表示 + enqueue
  lte.begin();
  lte.flush();           // 全件送信
  lte.end();
  delay(3000);
  M5.Display.sleep();
  esp_sleep_enable_timer_wakeup((uint64_t)SLEEP_INTERVAL_SEC * 1000000ULL);
  esp_deep_sleep_start();
  return;
}
```

---

## SORACOM への送信方式

**SORACOM Harvest Data（HTTP PUT）**
SIM カード認証のため API キー不要。

```
PUT http://unified.soracom.io
Content-Type: application/json

{"addr":"XX:XX:XX:XX:XX:XX","temp":23.4,"hum":55,"bat":80}
```

センサー1件につき1リクエスト（MAX_TARGETS = 10 件まで）。

---

## config.h に追加する定数

```cpp
static const uint8_t  LTE_TX_PIN      = 1;           // G1 (TXD2)
static const uint8_t  LTE_RX_PIN      = 2;           // G2 (RXD2)
static const char*    LTE_APN         = "soracom.io";
static const char*    LTE_USER        = "sora";
static const char*    LTE_PASS        = "sora";
static const char*    LTE_HOST        = "unified.soracom.io";
static const uint16_t LTE_TIMEOUT_MS  = 30000;
```

---

## 実装フェーズ

### Phase 1：単体疎通確認（別スケッチ）

**目標:** U128 が動作し、SORACOM への HTTP POST が成功すること

- [ ] U128 を Serial2（G1/G2）に接続
- [ ] AT コマンドで動作確認
  - `AT` → `OK`
  - `AT+CPIN?` → SIM 認識確認
  - `AT+CEREG?` → ネットワーク登録確認
- [ ] TinyGSM で SORACOM APN 接続
- [ ] HTTP PUT で Harvest Data へテストデータ送信成功

### Phase 2：`infra__lte` 実装

**目標:** `LteModem` クラスが単体テストできる状態

- [ ] `infra__lte.h / .cpp` 作成
- [ ] `begin()`: TinyGSM 初期化・APN 接続・タイムアウト処理
- [ ] `enqueue()`: 内部バッファへの追加（上限 MAX_TARGETS）
- [ ] `flush()`: バッファ全件を HTTP PUT、結果を Serial に出力
- [ ] `end()`: 切断・省電力状態へ
- [ ] 圏外・タイムアウト時はスキップして正常終了
- [ ] `config.h` に LTE 定数追加

### Phase 3：既存コードへの統合

**目標:** Deep Sleep モードでスキャン → 表示 → 送信 → スリープが動作

- [ ] `flushQueue()` に `lte.enqueue(d)` 追加
- [ ] Deep Sleep モードの `loop()` に `lte.begin/flush/end` 追加
- [ ] 送信中の表示（"Sending..." 等）を `view.message()` で追加（任意）
- [ ] 通常モードでも同様に動作確認

### Phase 4：安定化

- [ ] 圏外時の動作確認（スキップして Sleep へ移行）
- [ ] 送信失敗時のログ出力（Serial）
- [ ] LTE 起動時間の計測・`SLEEP_INTERVAL_SEC` 調整
- [ ] （任意）送信失敗データの RTC メモリ退避・次回送信

---

## アーキテクチャ上の決定事項

### BleScanner はコンシューマーを知らない

`scanner.queue` は単一の汎用キュー。誰が消費するかは Application 層（`.ino`）の責務。
BleScanner を変更せず LTE を追加できる。

### ファンアウトは Application 層

```
scanner.queue → flushQueue()
                  ├── view.sensorData(d)   // 表示
                  └── lte.enqueue(d)       // LTE バッファ
```

### 送信バッファは LteModem の内部実装

Application 層は `enqueue()` / `flush()` のみ呼ぶ。バッファ構造・再送は `LteModem` が管理。

### 表示→送信の順序保証

`flushQueue()` で全件表示 + enqueue → その後 `lte.flush()` の順を守る。
送信中に表示が途切れることはない。

---

## 将来の拡張（Phase 4 以降）

- **マルチコア化**: LTE 送信を Core 0 の専用タスクに分離（BLE scan と並列化）
- **バッテリー電圧監視**: ADC1 (G7/ACH6, G8/ACH7) で 2 系統を監視、ペイロードに追加
- **ULP バッテリー低下アラート**: 電圧低下時に LTE で警告送信してシャットダウン
