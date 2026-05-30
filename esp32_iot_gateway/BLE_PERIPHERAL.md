# BLE Peripheral 本番実装計画

検証実装（`src/ble_verify.cpp` + `mobile/lib/main.dart`）を踏まえた本番組み込みの設計書。

---

## 実現するフロー

1. ESP32 を操作（メニュー or GPIO0）し BLE スマホペアリングモードを起動
2. スマホのペアリング操作で OLED に表示された PIN を入力してボンディング
3. スマホと接続が続く限り ESP32 は起動し続け、スマホに情報を Notify し続ける
4. スマホ接続中も LTE 経由で従来と同じペースで MQTT 送信し続ける
5. スマホ切断 → 従来の DeepSleep ループに復帰
6. 通常フロー（TIMER wakeup）で起動時にボンド済みスマホが近くにいれば自動接続し、3+4 を繰り返す

---

## 全体方針

- BLE Peripheral と LTE は**同時動作**させる（NimBLE は Central+Peripheral 同時対応）
- EXT0 wakeup（GPIO0 ボタン）は**初回ペアリング専用**。通常の自動接続は TIMER wakeup で行う
- TIMER wakeup 起動直後から BLE アドバタイズを開始し、ボンド済みスマホが近くにいれば自動接続
- スマホ接続有無によって動作モードを分岐し、切断したら DeepSleep に戻る
- スマホ側アプリは `mobile/` の Flutter アプリをそのまま発展させる
- OLED ディスプレイを活かした Passkey ペアリングでセキュリティを担保する

---

## ファイル構成

### 新規作成

| ファイル | 層 | 内容 |
|---|---|---|
| `src/device/ble_peripheral.h` | device | `BlePeripheral` クラス宣言・UUID 定数 |
| `src/device/ble_peripheral.cpp` | device | GATT Server 実装・セキュリティコールバック |

### 変更

| ファイル | 変更内容 |
|---|---|
| `src/device/ble_scan.cpp` | `deinit()` から `NimBLEDevice::deinit(true)` を削除 |
| `src/device/oled.h/.cpp` | `oledShowPhonePairPin(uint32_t key)` を追加 |
| `src/service/menu.h/.cpp` | Phone Pair 画面・Op Mode サブメニュー追加 |
| `platformio.ini` | release / develop env の `PERIPHERAL_DISABLED` フラグを削除 |
| `src/main.cpp` | TIMER wakeup に BLE Peripheral 組み込み・EXT0 分岐・`DONE_DEEP_SLEEP` 対応 |

---

## GATT 構成

検証で確定した UUID をそのまま流用する。参照実装: `src/ble_verify.cpp`

**デバイス名**: `car-iot-ble`
**アドバタイズ UUID**: 計測サービス（ユニークなので誤検出しない）

### 計測サービス — Notify のみ / 全値 float32 little-endian

| Characteristic | UUID | 内容 |
|---|---|---|
| Service | `f3a8b2c1-d4e5-4f6a-7b8c-9d0e1f2a3b4c` | — |
| 電圧 | `f3a8b2c2-...` | V（INA228） |
| 電流 | `f3a8b2c3-...` | A（INA228） |
| 電力 | `f3a8b2c4-...` | W（INA228） |
| 温度 | `f3a8b2c5-...` | °C（INA228 ダイ温度） |

### 設定サービス — Read / Write with response / 暗号化必須

既存の `config.h` NVS 設定値を BLE 経由で読み書きする。新規 NVS キーは追加しない。
対象は `getChgStartV` / `setChgStartV`、`getChgStopV` / `setChgStopV` など既存のセッターに限定する。
具体的な Characteristic 定義は実装時に確定する。

設定 Characteristic には `READ_ENC | WRITE_ENC` を付与し、ペアリング済み接続のみ操作可能にする。

---

## セキュリティ（Passkey Entry + Bonding）

### ペアリング方式

| 項目 | 設定 |
|---|---|
| IO Capability | `BLE_HS_IO_DISPLAY_ONLY`（デバイス表示 / スマホ入力） |
| MITM 防止 | 有効 |
| Secure Connections | 有効 |
| Bonding | 有効（スマホを NVS に記憶、TIMER wakeup で自動再接続） |

### フロー

```
スマホが接続要求
  → OLED に 6桁 PIN 表示（random(100000, 999999)）
  → スマホユーザーが PIN を入力
  → 認証成功 → OLED "BLE paired" 表示
  → 失敗   → 即切断
```

### NimBLE 設定コード（`BlePeripheral::setup()` 内）

```cpp
NimBLEDevice::setSecurityAuth(
  BLE_SM_PAIR_AUTHREQ_BOND |
  BLE_SM_PAIR_AUTHREQ_MITM |
  BLE_SM_PAIR_AUTHREQ_SC
);
NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_ONLY);
```

### セキュリティコールバック（`ServerCallbacks` 内）

```cpp
uint32_t onPassKeyRequest() override {
  uint32_t key = random(100000, 999999);
  oledShowPhonePairPin(key);  // device/oled.h — 同層参照
  return key;
}

void onAuthenticationComplete(NimBLEConnInfo& info) override {
  if (!info.isEncrypted()) {
    NimBLEDevice::getServer()->disconnect(info.getConnHandle());
    return;
  }
  oledPrint("BLE paired");
}
```

---

## `BlePeripheral` クラスインターフェース

```cpp
// device/ble_peripheral.h
class BlePeripheral {
public:
  void setup();    // NimBLEDevice::init + Service/Char 生成 + セキュリティ設定
  void start();    // startAdvertising
  void stop();     // stopAdvertising
  bool isConnected() const;
  void notifyMeasurement(float voltage, float current, float power, float tempC);
};

extern BlePeripheral blePeripheral;
```

`onWrite` コールバックは `device/ble_peripheral.cpp` のファイルスコープ内クラスとして定義し、
既存の `config.h` セッターを直接呼ぶ。デコード → setter 呼び出しのみに留め、ビジネスロジックを書かない。

---

## 動作フロー（main.cpp）

### TIMER wakeup（通常フロー）

```
TIMER wakeup
  │
  ├─ oledInit / adsInit / ina228.init / bleScanner.setup() / bleTargets.load()
  ├─ blePeripheral.setup()
  ├─ blePeripheral.start()       ← アドバタイズ開始
  │                                 ボンド済みスマホが近くにいれば自動接続してくる
  ├─ lte.setup() / OTA / Jobs
  │
  ├─── [スマホ未接続] ───────────→ 従来の DEEP_SLEEP ループ
  │         measureAndPublish() → blePeripheral.stop() → DeepSleep
  │
  └─── [スマホ接続中] ───────────→ Phone+LTE モード（CONTINUOUS）
            │
            ├─ measureAndPublish() （5分ペース、既存ロジックそのまま）
            ├─ blePeripheral.notifyMeasurement() （毎秒）
            │
            └─ スマホ切断を検知
                 → 現在のサイクル完了
                 → blePeripheral.stop()
                 → DeepSleep
```

### EXT0 wakeup（初回ペアリング専用）

```
EXT0 wakeup（GPIO0 ボタン長押し）
  │
  ├─ oledInit / ina228.init
  ├─ blePeripheral.setup()
  ├─ blePeripheral.start()       ← アドバタイズ開始
  ├─ OLED "Waiting phone..."
  │
  ├─ 接続 + PIN 入力 → ボンディング完了
  │
  ├─ 30秒タイムアウト（接続なし）→ break
  │
  └─ blePeripheral.stop() → DeepSleep（以降は TIMER wakeup で自動接続）
```

LTE は起動しない。ペアリング成立だけが目的。

### DeepSleep 移行（共通）

```cpp
esp_sleep_enable_ext0_wakeup(GPIO_NUM_0, 0);  // TIMER 側にも追加
esp_sleep_enable_timer_wakeup(...);
esp_deep_sleep_start();
```

---

## ble_scan.cpp の変更

```cpp
// 変更前
void BleScanner::deinit() {
  NimBLEDevice::deinit(true);  // BLE スタックを完全終了
}

// 変更後
void BleScanner::deinit() {
  // NimBLEDevice::deinit() を呼ばない
  // Peripheral と BLE スタックを共有するため
}
```

NimBLE は Central（スキャナー）と Peripheral（アドバタイズ）を同時動作できる。
TIMER wakeup で `bleScanner.setup()` と `blePeripheral.setup()` を両方呼ぶが、
`NimBLEDevice::init()` は初回のみ有効（2回目以降は no-op）なので競合しない。

---

## platformio.ini の変更

```ini
# release env / develop env から以下を削除
-D CONFIG_BT_NIMBLE_ROLE_PERIPHERAL_DISABLED=1
-D CONFIG_BT_NIMBLE_ROLE_BROADCASTER_DISABLED=1
-D CONFIG_BT_NIMBLE_MAX_CONNECTIONS=0   # 1 に変更（接続1台分を確保）
```

フラッシュサイズは `ble_verify` 実績から概ね +16 KB 程度の増加。

---

## 設定メニュー追加項目

`service/menu.h/.cpp` を変更する。既存の MENU.md 仕様に追記する形で設計する。

### メニュー構成変更

```
[ルート "/"]
  ├── BLE Settings    → ["/BLE Settings"]（既存）
  │   ├── Register    → （既存）
  │   └── Remove      → （既存）
  ├── Phone Pair      → BLE_PHONE_PAIR 画面（NEW）
  ├── Sensor View     → （既存）
  ├── Op Mode         → ["/Op Mode"]（既存の "Continuous" を置き換え）
  │   ├── Deep Sleep  → DONE_DEEP_SLEEP（NEW）
  │   └── Continuous  → DONE_CONTINUOUS（既存アクションを移動）
  ├── System          → ["/System"]（既存）
  └── Restart         → （既存）
```

### 新規 MenuState

| 状態 | 説明 |
|---|---|
| `BLE_PHONE_PAIR` | BLE Peripheral を起動してスマホのペアリングを待つ |
| `DONE_DEEP_SLEEP` | DEEP_SLEEP を返して `enterMenuMode()` を終了 |

### ステートマシン追加分

```
MENU_NAV(root)
  ├─(Phone Pair)──→ BLE_PHONE_PAIR ──(完了/タイムアウト/BTN1長押し)──→ MENU_NAV
  └─(Op Mode)─────→ MENU_NAV("/Op Mode")
       ├─(Deep Sleep)──→ DONE_DEEP_SLEEP（enterMenuMode が DEEP_SLEEP を返す）
       └─(Continuous)──→ DONE_CONTINUOUS（既存）
```

### BLE_PHONE_PAIR 画面フロー

```
[待機中]                [PIN 表示中]           [完了]
Phone Pair              Phone Pair              Phone Pair
──────────────────      ──────────────────      ──────────────────
Waiting...              PIN: 123456             Paired!

BTN1 long: cancel       Enter on phone          BTN1: back
```

- 画面に入った時点で `blePeripheral.setup()` + `blePeripheral.start()` を呼ぶ
- `onPassKeyRequest()` → `oledShowPhonePairPin(key)` で PIN 表示に切り替え
- `onAuthenticationComplete()` → 成功時は OLED を "Paired!" に更新、失敗時は即切断
- BTN1 長押しまたは 60 秒タイムアウトで `blePeripheral.stop()` → MENU_NAV へ

---

## 実装順序

1. `device/ble_peripheral.h/.cpp` — `ble_verify.cpp` から移植、セキュリティ追加
2. `device/oled.h/.cpp` — `oledShowPhonePairPin()` 追加
3. `service/menu.h/.cpp` — Phone Pair 画面・Op Mode サブメニュー追加
4. `device/ble_scan.cpp` — `deinit()` 修正
5. `platformio.ini` — DISABLED フラグ削除
6. `main.cpp` — TIMER wakeup に BLE Peripheral 組み込み・EXT0 分岐追加
7. develop env でビルド確認 → 動作検証 → コミット
