# デュアルコア活用 実装計画

ESP32-S3 の 2 コアを活かした 3 つの改善案。それぞれ独立して実装できる。

---

## 前提条件（別セッションで実装する場合はここから読む）

### ハードウェア構成

| 項目 | 内容 |
|------|------|
| MCU | ESP32-S3-MINI-1-N8（デュアルコア Xtensa LX7、240MHz） |
| ADC | ADS1115（I2C アドレス 0x48）― `device/ads.h/.cpp` |
| 電流計 | INA228（I2C アドレス 0x40）― `device/ina228.h/.cpp` |
| OLED | SSD1306 128×64（I2C アドレス 0x3C）― `device/oled.h/.cpp` |
| LTE | SIM7080G（UART2: RX=GPIO7, TX=GPIO8）― `device/lte.h/.cpp` |
| BLE | ESP32 内蔵（SwitchBot センサー受信用）― `device/ble_scan.h/.cpp` |

**I2C バス**: ADS1115・INA228・OLED の 3 デバイスが **同一の `Wire` オブジェクト**（SDA=GPIO17, SCL=GPIO18）を共有している。
`Wire.begin()` は `oledInit()` の中で呼ばれており、`adsInit()` と `ina228Init()` はそれ以降に初期化する前提。

### コア割り当て（ESP32-S3 + Arduino）

| コア | 役割 | 注意 |
|------|------|------|
| Core 0 | Protocol CPU。BLE スタック（esp_bt）が常駐。Arduino タスクは存在しない | BLE コールバック（`onResult`）はここで呼ばれる |
| Core 1 | Application CPU。Arduino の `setup()` / `loop()` タスクが動く | デフォルトではこちらのみ使用 |

`xTaskCreatePinnedToCore(fn, name, stack, arg, priority, handle, coreId)` でコアを指定してタスクを生成する。

### FreeRTOS の現在の使用状況

`device/ble_scan.cpp` で以下のみ使用中：

```cpp
// ble_scan.cpp
queue = xQueueCreate(QUEUE_SIZE, sizeof(SensorVariant)); // キュー生成（QUEUE_SIZE=20）
xQueueSend(bleScanner.queue, &v, 0);                    // Core 0 コールバックから書き込み

// main.cpp
xQueueReceive(bleScanner.queue, &v, 0);                 // Core 1 から読み出し
```

Mutex・セマフォ・タスク生成は**現状まだ使われていない**。今回の実装で初めて導入する。

### 現在の起動シーケンス（`main.cpp` の `setup()`）

```
oledInit()           ← Wire.begin() もここで呼ばれる
adsInit()
ina228Init()
oledPrint(...)

lte.setup()          ← ~20-40秒（電源ON → APN設定 → GPRS接続 → 時刻同期 → TLS証明書設定）
ota.check()          ← ~5-10秒（MQTT接続 → Jobs API ポーリング）

[DEBUG_MODE 時のみ以降を実行]
bleTargets.load()
bleScanner.setup()
bleScanner.start(SCAN_TIME)  ← SCAN_TIME=10秒、ブロッキング
bleScanner.clearResults()
bleScanner.deinit()

xQueueReceive(bleScanner.queue, ...)  ← BLE センサーデータ処理
adsReadDiff01/23()
ina228ReadCurrent/Power/Temp()
mqtt.publish()
lte.powerOff()
```

本番モード（DEBUG_MODE 未定義）では BLE スキャンを実行せず DeepSleep に入る（現状 BLE 送信はコメントアウト中）。

### Wire ライブラリのスレッド安全性

**Arduino の `Wire` ライブラリはスレッドセーフではない。**

`Wire.beginTransmission()` → `Wire.write()` → `Wire.endTransmission()` のシーケンスは分割不可。複数コアから同時に呼ぶと I2C トランザクションが破損する。

`ads.cpp`・`ina228.cpp`・`oled.cpp` はすべて `Wire` オブジェクト（グローバル）を直接使っている。これらを複数コアから呼ぶ場合は **FreeRTOS の `SemaphoreHandle_t`（Mutex）で排他制御が必須**。

```cpp
// Mutex の基本パターン
SemaphoreHandle_t g_i2cMutex = xSemaphoreCreateMutex();

// I2C を使う前に取得、使い終わったら返却
if (xSemaphoreTake(g_i2cMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
  // Wire を使う処理
  xSemaphoreGive(g_i2cMutex);
}
```

Mutex は使用するタスクを起動する**前**に生成しておくこと（`setup()` の早い段階で `xSemaphoreCreateMutex()` を呼ぶ）。

### BLE スキャンの動作詳細

`BleScanner::start(int seconds)` の実装:

```cpp
void BleScanner::start(int seconds) {
  _scan->start(seconds, false);  // 第2引数は is_continue（継続スキャンか否か）
}
```

`_scan->start(seconds, false)` は呼び出し元タスクを `seconds` 秒間ブロックする。
「ブロッキング」には 2 種類あり、どちらかによって Core 0 への I2C タスク同居が可能かどうかが変わる。

| 種類 | 挙動 | Core の空き |
|------|------|------------|
| ビジー待ち（`while(!done){}`） | CPU を占有し続ける | **なし**（I2C タスクは動けない） |
| セマフォ待ち（`xSemaphoreTake`） | タスクを眠らせてスケジューラに返す | **あり**（I2C タスクが動ける） |

**ESP32 Arduino BLE ライブラリの実装**: `esp_ble_gap_start_scanning()` でハードウェアにスキャンを委ねた後、完了イベントまで内部セマフォで待機する（セマフォ待ち）。よって `start()` はタスクをブロックするが Core 0 のスケジューラはフリーになる。

```
Core 0 タスク A（BLE）: start() → xSemaphoreTake → 眠る
                                    ↓ スケジューラが B に切り替える
Core 0 タスク B（I2C）:              センサー読み取り → 200ms 待ち → 読み取り → ...
BLE コールバック:                    ↑ onResult（数μs、割り込み的に Core 0 で動く）
```

この挙動により **`start()` を真に非同期化しなくても、別タスクにするだけで I2C と同居できる**。

ただしこれはライブラリ実装に依存した前提。もし実際には I2C サンプリングが動いていない（サンプル数がゼロ）場合は、ビジー待ちの可能性があるため真の非同期化（コールバック形式 `_scan->start(duration, callback, is_continue)`）に切り替える。

BLE コールバック（`SwitchBotCallback::onResult`）は内部的に **Core 0** で呼ばれる。`xQueueSend` を呼ぶだけなので数μs で完了し、I2C タスクとの競合は実質無視できる。

案 1（BLE+LTE 並列化）および案 2（連続サンプリング）ともに、**`BleScanner` クラスの変更は不要**。`main.cpp` 内で `xTaskCreatePinnedToCore` を直接呼ぶ。

### LTE（SIM7080G）の特性

- `SerialAT`（`Serial2`、UART2）を使用。I2C・BLE と資源が被らない
- `lte.setup()` はブロッキング。内部では AT コマンドのタイムアウト待ちが多く、実測 20〜40 秒
- `TinyGSM` ライブラリはスレッドセーフではないが、LTE 操作は Core 1 のみで行うので問題なし
- `lte.sendCmdResp()` / `lte.sendCmd()` は `SerialAT` を直接操作するため、複数コアからの同時呼び出しは禁止

### ADS1115 の読み取り時間

`ads.readADC_Differential_0_1()` は内部で変換完了を**ポーリング待ち**する（Adafruit ADS1X15 ライブラリのデフォルト動作）。現在のゲイン設定（`GAIN_FOUR`）でのサンプリングレートはデフォルト 128SPS → 1 読み取りあたり約 **8ms** のブロッキングが発生する。差動 2 ch 読み取りで合計約 16ms。

### INA228 の読み取り時間

`ina228.cpp` は `Wire.h` を直接使用（ライブラリなし）。`Wire.requestFrom()` は I2C の SCL クロックレート依存（Arduino デフォルト 100kHz）。3 バイト読み取りで数百 μs。4 関数（VBus・Current・Power・Temp）呼んでも合計 2ms 未満。

### OLED の描画時間

`display.display()` は SSD1306 の全フレームバッファ（1024 バイト）を I2C で転送する。100kHz I2C で約 **10ms** かかる。描画関数（`clearDisplay`・`printf`・`display`）をまとめると 1 回の `oledShowStatus()` 呼び出しで 10〜15ms のブロッキング。

### FreeRTOS タスクのスタックサイズ目安

| タスク内容 | 推奨スタックサイズ |
|------------|-------------------|
| BLE スキャン待機のみ | 4096 bytes |
| I2C 読み取り（ADS + INA228） | 4096 bytes |
| OLED 描画（Adafruit ライブラリ使用） | 4096〜8192 bytes |

スタック不足は `configCHECK_FOR_STACK_OVERFLOW` で検出できるが、Arduino ビルドではデフォルト無効。スタックオーバーフローはサイレントにクラッシュするので不明なクラッシュが出たらスタックサイズを増やすこと。

### タスク優先度の考え方

Arduino の `loop()` タスクの優先度は `1`。新しく作るタスクの優先度は:

| タスク | 推奨優先度 | 理由 |
|--------|-----------|------|
| BLE スキャン専用タスク | 1 | BLE スタック（優先度 22）が実処理を担うので低くていい |
| センサーサンプリング | 1〜2 | loop() と同等か少し高め |
| OLED 更新 | 1 | 表示の遅延は許容できる |

### 既存ファイルへの影響まとめ

各案の実装で変更が必要になるファイル：

| ファイル | 案 1 | 案 2 | 案 3 |
|----------|:---:|:---:|:---:|
| `src/main.cpp` | 変更（5行） | 変更（10行） | 変更（DEBUG_MODE 内） |
| `src/device/ble_scan.h/.cpp` | 変更なし | 変更なし | 変更なし |
| `src/device/ads.cpp` | 変更なし | Mutex 追加 | Mutex 追加 |
| `src/device/ina228.cpp` | 変更なし | Mutex 追加 | Mutex 追加 |
| `src/device/oled.cpp` | 変更なし | Mutex 追加 | **変更**（タスク化） |
| `src/device/sensor_sampler.h/.cpp` | 不要 | **新規** | 不要 |
| `src/domain/sample_buffer.h/.cpp` | 不要 | **新規** | 不要 |
| `src/config.h` | 変更なし | 定数追加 | 変更なし |
| `src/domain/telemetry.h/.cpp` | 変更なし | 関数追加 | 変更なし |

---

## 前提知識

| コア | 役割 | Arduino での扱い |
|------|------|-----------------|
| Core 0 | Protocol CPU。BLE スタックが常駐 | BLE コールバックはここで動く |
| Core 1 | Application CPU | `setup()` / `loop()` が動く |

FreeRTOS はすでに使用中（`ble_scan.cpp` で `xQueueCreate` / `xQueueSend`）。
I2C バスは ADS1115・INA228・OLED が共有（SDA=GPIO17, SCL=GPIO18）→ **複数コアから触る場合は必ず Mutex が必要**。

---

## 案 1: LTE 接続 ↔ BLE スキャンの並列化

### 目的

起動シーケンスを短縮する。現状の逐次フローを並列化し、**起動時間を最大 `SCAN_TIME` 秒（現在 10 秒）削減**する。

### 現状フロー

```
Core 1 (setup):
  lte.setup()        ← ~30 秒（GPRS 接続・TLS ネゴシエーション）
  ota.check()        ← 数秒
  bleScanner.start() ← SCAN_TIME 秒（ブロッキング）
  センサー読み取り
  mqtt.publish()
  DeepSleep
```

### 変更後フロー

```
Core 0 (BLE タスク):    BleScanner::start() ← SCAN_TIME 秒でフラグを立てる
Core 1 (main):         lte.setup() → ota.check() → MQTT 接続
                         ↓ EventGroup で両方の完了を待つ
                       センサー読み取り → mqtt.publish() → DeepSleep
```

LTE と BLE は資源（UART vs RF/BLE スタック）が被らないので干渉しない。

### 実装手順

#### Step 1: `main.cpp` に BLE タスクを追加

`BleScanner` クラスは変更しない。`main.cpp` 内に static なタスク関数と完了フラグを定義し、`xTaskCreatePinnedToCore` で Core 0 に起動する。

```cpp
// main.cpp 冒頭に追加
static volatile bool s_bleDone = false;

static void bleScanTask(void *) {
  bleScanner.start(SCAN_TIME); // Core 0 でブロック（内部はセマフォ待ちで Core 0 を解放）
  bleScanner.clearResults();
  s_bleDone = true;
  vTaskDelete(nullptr);
}
```

#### Step 2: `setup()` の起動シーケンスを並列化

```cpp
// setup() 内の変更箇所

bleTargets.load();
bleScanner.setup();
xTaskCreatePinnedToCore(bleScanTask, "ble_scan", 4096,
                        nullptr, 1, nullptr, 0); // Core 0 で BLE スキャン開始

lte.setup();   // Core 1 で LTE 接続（同時進行）
ota.check();

// LTE が先に終わった場合、BLE スキャン完了を待つ
while (!s_bleDone) {
  delay(100);
}

// 以降は従来通り
SensorVariant v;
while (xQueueReceive(bleScanner.queue, &v, 0) == pdTRUE) { ... }

VoltageReading v1 = {adsReadDiff01()};
// ...
```

#### Step 3: DeepSleep 前のクリーンアップ確認

`bleScanner.deinit()` は `s_bleDone == true` になった後に呼ぶ。`while (!s_bleDone)` を抜けた後であれば順序は保証される。

### 注意点

- `BLEDevice::init("")` は `bleScanner.setup()` 内で呼ばれている。タスク起動前に `setup()` を済ませること
- BLE スタックは Core 0 に常駐しているので Core 0 にピン留めする（`coreId=0`）
- `start()` がセマフォ待ちではなくビジー待ちだった場合、LTE との並列化はできるが **Core 0 での I2C 同居（案 2）はできない**。その場合は `_scan->start(duration, callback, is_continue)` のコールバック形式に切り替える

### 期待効果

| 項目 | 現状 | 変更後 |
|------|------|--------|
| 起動時間 | LTE(30s) + BLE(10s) = ~40s | max(LTE, BLE) = ~30s |
| コード変更量 | — | `main.cpp` のみ 15行程度 |

---

## 案 2: 連続センサーサンプリング + イベント検出

### 目的

5 分に 1 回の1点スナップショットを、**起動してから送信するまでの全サンプルのヒストリ**に変える。電圧急落・過電流スパイクなどのイベントも検出できるようにする。

### 現状の問題

```
// 現状: mqtt.publish() 直前の 1 点だけ
VoltageReading v1 = {adsReadDiff01()};
VoltageReading v2 = {adsReadDiff23()};
PowerReading pwr = {ina228ReadCurrent(), ina228ReadPower(), ina228ReadTemp()};
buildShadowPayload(payload, sizeof(payload), v1, v2, pwr, now);
```

- 突入電流や電圧降下は捉えられない
- 通信ノイズ由来の外れ値がそのまま送信される

### 変更後の構成

```
Core 0 (サンプリングタスク):
  200ms ごとに ADS1115 + INA228 を読み取り → RingBuffer に積む
  過電流・電圧急落を検出したらイベントフラグを立てる

Core 1 (main):
  lte.setup() → ota.check()
  サンプリングタスクを停止
  RingBuffer から統計値を計算（平均・最大・最小・イベント有無）
  mqtt.publish() → DeepSleep
```

### 実装手順

#### Step 1: `domain/` に `SampleBuffer` を追加

domain 層はハードウェアに依存しないので、リングバッファとイベント定義はここに置く。

```cpp
// domain/sample_buffer.h（新規）

#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

static const int SAMPLE_BUF_SIZE = 256; // 200ms * 256 = ~51 秒分

struct SensorSample {
  float v1;       // サブバッテリー電圧 (V)
  float v2;       // メインバッテリー電圧 (V)
  float current;  // 電流 (A)
  float power;    // 電力 (W)
  float temp;     // 温度 (°C)
};

struct SampleStats {
  SensorSample avg;
  SensorSample max;
  SensorSample min;
  int count;
  bool overcurrentDetected;  // current > OVERCURRENT_THRESHOLD_A
  bool voltageDipDetected;   // v1 or v2 が DIP_THRESHOLD_V を下回った
};

class SampleBuffer {
public:
  void push(const SensorSample &s);
  SampleStats compute() const;
  void clear();
  int count() const { return _count; }

private:
  SensorSample _buf[SAMPLE_BUF_SIZE];
  int _head = 0;
  int _count = 0;
  SemaphoreHandle_t _mutex = nullptr; // 初期化は setup() で行う
  friend void sampleBufferInit();
};

extern SampleBuffer sampleBuffer;
```

閾値は `config.h` に定数として追加する：

```cpp
// config.h に追加
static const float OVERCURRENT_THRESHOLD_A = 60.0f; // A
static const float VOLTAGE_DIP_THRESHOLD_V = 11.5f; // V
static const int SAMPLE_INTERVAL_MS = 200;
```

#### Step 2: `device/` に `sensor_sampler` タスクを追加

I2C は ADS/INA228/OLED が共有しているので **Mutex 必須**。Mutex のハンドルは `sensor_sampler.cpp` 内で管理し、`adsReadDiff01()` / `ina228ReadCurrent()` 等をラップして呼ぶ。

```cpp
// device/sensor_sampler.h（新規）

#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// I2C Mutex（ads.cpp / ina228.cpp / oled.cpp が共有する）
extern SemaphoreHandle_t g_i2cMutex;

void sensorSamplerStart(); // Core 0 にタスクを起動
void sensorSamplerStop();  // タスクを停止して join
```

```cpp
// device/sensor_sampler.cpp（新規・実装イメージ）

#include "sensor_sampler.h"
#include "ads.h"
#include "ina228.h"
#include "../domain/sample_buffer.h"
#include "../config.h"

SemaphoreHandle_t g_i2cMutex = nullptr;
static TaskHandle_t s_task = nullptr;

static void samplerTask(void *) {
  while (true) {
    SensorSample s;
    if (xSemaphoreTake(g_i2cMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
      s.v1      = adsReadDiff01();
      s.v2      = adsReadDiff23();
      s.current = ina228ReadCurrent();
      s.power   = ina228ReadPower();
      s.temp    = ina228ReadTemp();
      xSemaphoreGive(g_i2cMutex);
    }
    sampleBuffer.push(s);
    vTaskDelay(pdMS_TO_TICKS(SAMPLE_INTERVAL_MS));
  }
}

void sensorSamplerStart() {
  g_i2cMutex = xSemaphoreCreateMutex();
  xTaskCreatePinnedToCore(samplerTask, "sensor_sampler", 4096,
                          nullptr, 2, &s_task, 0); // Core 0
}

void sensorSamplerStop() {
  if (s_task) {
    vTaskDelete(s_task);
    s_task = nullptr;
  }
}
```

#### Step 3: `oled.cpp` も Mutex を取るよう修正

```cpp
// oled.cpp の表示関数の先頭に追加
if (g_i2cMutex && xSemaphoreTake(g_i2cMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
  // ... 既存の display.display() 処理 ...
  xSemaphoreGive(g_i2cMutex);
}
```

#### Step 4: `domain/telemetry` にヒストリペイロードを追加

```cpp
// telemetry.h に追加
int buildShadowPayloadFromStats(char *buf, size_t size,
                                const SampleStats &stats,
                                time_t ts);
```

ペイロード例：
```json
{
  "state": {
    "reported": {
      "v1_avg": 12.34, "v1_max": 12.50, "v1_min": 11.80,
      "v2_avg": 12.10, "v2_max": 12.28, "v2_min": 11.90,
      "current_avg": 5.21, "current_max": 62.1,
      "temp": 28.5,
      "samples": 142,
      "overcurrent": false,
      "voltage_dip": true,
      "ts": 1746143400
    }
  }
}
```

#### Step 5: `main.cpp` の変更

```cpp
void setup() {
  // ...既存の初期化...
  adsInit();
  ina228Init();

  sensorSamplerStart(); // ← ここから Core 0 でサンプリング開始

  lte.setup();          // ← Core 1 で LTE 接続（同時進行）
  ota.check();
  if (mqtt.isConnected()) ota.confirmBoot();

  sensorSamplerStop();  // ← LTE/OTA が終わったらサンプリング停止

  SampleStats stats = sampleBuffer.compute();
  // stats から buildShadowPayloadFromStats() でペイロード生成
  mqtt.publish(shadowTopic, payload);
  // ...DeepSleep...
}
```

### 注意点

- ADS1115 は I2C アドレス 0x48、INA228 は 0x40、OLED は 0x3C：バスは共有だが別アドレス。Mutex さえあれば競合しない
- `adsReadDiff01()` 等は内部でウェイト（ADS1115 のサンプリング完了待ち）がある。200ms 間隔なら問題ないが、タスク優先度は低め（2）にしておく
- ペイロードサイズが増えるので `payload[256]` を `payload[512]` 程度に拡張する

### 期待効果

| 項目 | 現状 | 変更後 |
|------|------|--------|
| 送信データ | 1 点スナップショット | 平均・最大・最小 + イベントフラグ |
| 外れ値耐性 | なし | 平均化でノイズ吸収 |
| イベント検出 | なし | 過電流・電圧急落を検出 |
| コード変更量 | — | 新規 3 ファイル、`main.cpp` 10 行 |

---

## 案 3: ヘルスモニタを Core 0 に常駐（DEBUG_MODE 限定）

### 目的

`DEBUG_MODE` のときの `loop()` を改善する。現状は 3 秒ごとに電圧読み取りと OLED 更新を交互に行っているが、**OLED 更新（I2C 書き込み）と処理が干渉**している。Core を分けることで両者をノンブロッキングにする。

### 現状の `loop()` の問題

```cpp
void loop() {
  float voltage = adsReadDiff01();   // I2C
  oledShowStatus(voltage, ...);      // I2C（同じバス、同じコア）
  delay(3000);                       // ← その間リレー制御も止まる
  // ...
}
```

3 秒周期でしか動かない。ボタン読み取り・リレー制御・センサー読み取りがすべてこの周期に縛られている。

### 変更後の構成

```
Core 0 (OLED タスク):
  1 秒ごとに OLED を更新（最新の計測値を表示する）
  I2C Mutex を取ってから display.display() を呼ぶ

Core 1 (loop):
  センサー読み取り（100ms ごと）
  ボタン読み取り
  リレー制御
  millis() ベースのノンブロッキング処理
  I2C Mutex を取ってから ADS/INA228 を読む
```

### 実装手順

#### Step 1: OLED タスクを `device/oled` に追加

案 2 の `g_i2cMutex` を共用する（案 2 と同時実装なら）。単独実装なら `oled.cpp` 内で定義する。

```cpp
// oled.h に追加
void oledTaskStart(); // Core 0 にタスクを起動
void oledTaskStop();
void oledSetData(float v1, float v2, bool relayOn, bool btn0, bool btn1);
```

```cpp
// oled.cpp に追加（実装イメージ）

struct OledData {
  float v1, v2;
  bool relayOn, btn0, btn1;
};

static volatile OledData s_data = {};
static TaskHandle_t s_oledTask = nullptr;

static void oledTaskFn(void *) {
  while (true) {
    OledData d = s_data; // volatile コピー（atomic でないが表示用途なら許容）
    if (xSemaphoreTake(g_i2cMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
      oledShowStatus(d.v1, d.v2, d.relayOn, d.btn0, d.btn1);
      xSemaphoreGive(g_i2cMutex);
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

void oledTaskStart() {
  if (!g_i2cMutex) g_i2cMutex = xSemaphoreCreateMutex();
  xTaskCreatePinnedToCore(oledTaskFn, "oled", 4096,
                          nullptr, 1, &s_oledTask, 0); // Core 0
}

void oledSetData(float v1, float v2, bool relayOn, bool btn0, bool btn1) {
  s_data = {v1, v2, relayOn, btn0, btn1};
}
```

#### Step 2: `loop()` をノンブロッキングに書き直す

```cpp
#ifdef DEBUG_MODE

static unsigned long s_lastSample = 0;
static unsigned long s_lastRelay = 0;
static bool s_relayState = false;

void loop() {
  unsigned long now = millis();
  bool btn0 = digitalRead(BTN0_PIN) == LOW;
  bool btn1 = digitalRead(BTN1_PIN) == LOW;

  // センサー: 500ms ごとに読み取り
  if (now - s_lastSample >= 500) {
    float v1, v2;
    if (xSemaphoreTake(g_i2cMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
      v1 = adsReadDiff01();
      v2 = adsReadDiff23();
      xSemaphoreGive(g_i2cMutex);
    }
    oledSetData(v1, v2, s_relayState, btn0, btn1); // OLED タスクに渡す
    s_lastSample = now;
  }

  // リレー: 6 秒周期で ON/OFF トグル
  if (now - s_lastRelay >= 3000) {
    s_relayState = !s_relayState;
    digitalWrite(RELAY_2_PIN, s_relayState ? HIGH : LOW);
    digitalWrite(CHG_ON_PIN,  s_relayState ? HIGH : LOW);
    s_lastRelay = now;
  }
}

#endif
```

#### Step 3: `setup()` の末尾で OLED タスクを起動

```cpp
#ifdef DEBUG_MODE
  oledTaskStart(); // ← DEBUG_MODE 時のみ
#endif
```

### 注意点

- `s_data` の読み書きは 2 コアから同時に起きうる。表示用途（多少の古いデータは許容）なので Mutex は不要だが、`float` は 4 バイト境界アクセスで事実上アトミックに動く。厳密にしたいなら `portMUX_TYPE` で保護する
- OLED I2C と ADS/INA228 I2C は同一バス。Mutex で排他すれば問題ない
- この案は `DEBUG_MODE` 限定。本番モードは DeepSleep するので常駐タスクは意味をなさない

### 期待効果

| 項目 | 現状 | 変更後 |
|------|------|--------|
| ボタン応答性 | 3 秒ごと | 数 ms |
| OLED 更新 | 処理と同期（3 秒周期） | 独立（1 秒周期） |
| リレー制御 | `delay` に縛られる | millis ベースで正確 |
| コード変更量 | — | `oled.h/.cpp` 30 行、`loop()` 書き直し |

---

## 3 案の比較

| | 案 1: BLE+LTE 並列化 | 案 2: 連続サンプリング | 案 3: ヘルスモニタ |
|--|--|--|--|
| 効果 | 起動時間削減 | データ品質向上 | DEBUG_MODE の使い勝手改善 |
| 対象 | 本番モード | 本番モード | DEBUG_MODE のみ |
| 難易度 | 低（タスク 1 本追加） | 中（Mutex + 新クラス） | 低（タスク 1 本 + loop 書き直し） |
| I2C Mutex 必要 | 不要 | **必要** | **必要** |
| 依存関係 | 独立 | 独立 | 案 2 と Mutex 共用可 |
| 推奨実装順 | 1 → 3 → 2 | | |

案 2 は I2C Mutex の導入が核心部分。案 3 も同じ Mutex が必要なので、**案 2 を実装すれば案 3 は差分が少ない**。

案 1 は資源の競合がなく最もリスクが低い。先に着手するのが無難。
