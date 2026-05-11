# メニュー画面 実装仕様

OLED と 2 ボタンで操作できる設定メニュー。主目的は BLE 機器の登録・削除。**実装済み。**

---

## ハードウェア前提

| 項目 | 内容 |
|------|------|
| OLED | SSD1306 128×64（I2C 0x3C）、`device/oled.h/.cpp` |
| BTN0 | GPIO26、INPUT_PULLUP、LOW = 押下 |
| BTN1 | GPIO33、INPUT_PULLUP、LOW = 押下 |
| ブザー | GPIO34、負論理（HIGH = 無音）、`device/speaker.h/.cpp` |
| BLE 登録先 | `BleTargets`（NVS、最大 `MAX_TARGETS=10` 件）|

---

## ボタン UX

| ボタン | 短押し（< 1s） | 長押し（≥ 1s） |
|--------|--------------|--------------|
| BTN0 | カーソル移動（下へ） | — |
| BTN1 | 決定 / 選択 | 戻る / キャンセル |

`device/button.h/.cpp` の `Button` クラスが `millis()` ベースで判定する。
`button.begin()` は `setup()` 内で呼ぶ（ピン設定も内部で行う）。

---

## 起動フロー

```
setup():
  speakerInit()
  playMelody(bootStart)   ← 起動開始音（C5 単音）
  button.begin(BTN0, BTN1)

  delay(300ms)            ← ボタン安定待ち（bootStart と兼用）
  if BTN0 == LOW:
    oledPrint("Menu Mode")
    enterMenuMode()       ← 戻らない（Restart 選択で esp_restart()）
  else:
    lte.setup() → OTA → DeepSleep（通常フロー）

  playMelody(boot)        ← 起動完了音（3音上昇）
```

`enterMenuMode()` は LTE を起動しない。メニューから抜けるには「Restart」を選択して再起動する。

---

## メニュー構成

```
[ルート "/"]
  ├── BLE Settings    → ["/BLE Settings"]
  │   ├── Register    → [スキャン] → [結果一覧・登録]
  │   └── Remove      → [登録済み一覧・削除確認]
  ├── Sensor View     → [センサーリアルタイム表示]
  ├── System          → ["/System"]
  │   ├── Info        → [FW バージョン・デバイス ID]
  │   └── NVS Clear   → [確認] → nvs_flash_erase() + esp_restart()
  ├── Continuous      → 継続計測モードに移行
  └── Restart         → esp_restart()
```

BTN1 長押しでひとつ上の階層に戻る（ルートでは何もしない）。

---

## 実装アーキテクチャ

### MenuItem 定義（フラット配列）

```cpp
struct MenuItem {
  const char* label;   // 表示文字列
  const char* path;    // 属する階層パス（例: "/", "/BLE Settings"）
  MenuAction  action;  // NONE = サブメニューを持つ、それ以外 = アクション実行
};
```

### ナビゲーション状態

```cpp
static char s_currentPath[64];  // 現在の階層（例: "/"、"/System"）
static int  s_index;            // 現在階層内のカーソル位置
```

`tickMenuNav()` は毎ループ `s_currentPath` と一致する MenuItem を抽出して表示する。  
サブメニューに入るときは `pathPush(label)` で `s_currentPath` を更新する（メモリ上の文字列操作のみ、スタック不要）。  
戻るときは `pathPop()` で末尾セグメントを削除する。

### 各画面

**ルートメニュー / サブメニュー**（MENU_NAV 状態）
```
Menu              ← pathTitle()（ルートは "Menu"、サブは最終セグメント）
──────────────────────
> BLE Settings
  Sensor View
  System
  Continuous
  Restart
```

**BLE スキャン中**（ブロッキング 10 秒）
```
BLE Scanning...
(10 sec)
```

**BLE 登録結果**（スキャン完了後）
```
BLE Register
──────────────────────
> aa:bb:cc:dd:ee:ff
  ff:ee:dd:cc:bb:aa
```
BTN1 短押しで選択・登録 → NVS 保存 → MENU_NAV に戻る。BTN1 長押しでキャンセル。

**BLE 削除**（登録済み一覧を兼ねる）
```
Remove (2)
──────────────────────
> aa:bb:cc:dd:ee:ff
  ff:ee:dd:cc:bb:aa
```
BTN1 短押しで確認画面へ。BTN1 長押しで MENU_NAV に戻る。登録 0 件時は "No registered" を表示。

**削除確認**
```
Delete?
aa:bb:cc:dd:ee:ff
────────────────────
  [Yes]       [No]
BTN1 long: cancel
```
BTN0 で Yes / No 切り替え、BTN1 短押しで確定。BTN1 長押しでキャンセル（削除画面に戻る）。

**センサー確認**（50ms 更新）
```
V1:12.34V V2:12.10V
Cur: 5.21A
Pwr: 62.5W
Tmp: 28.5C


BTN1 long: back
```

**システム情報**
```
1.3.0+a364343b
esp32-gw-aabbccdd
```

**NVS クリア確認**
```
NVS Clear?
All data lost!
────────────────────
  [Yes]       [No]
BTN1 long: cancel
```
BTN0 で Yes / No 切り替え、BTN1 短押しで確定。Yes 選択時は `nvs_flash_erase()` + `esp_restart()`。

---

## ステートマシン

```
MENU_NAV (汎用ナビ)
 ├─(BLE Settings)────→ MENU_NAV("/BLE Settings")
 │   ├─(Register)────→ BLE_SCAN ──→ BLE_SCAN_RESULT ──(登録/キャンセル)──→ MENU_NAV
 │   └─(Remove)──────→ BLE_REMOVE ──(BTN1 短押し)──→ BLE_REMOVE_CONFIRM
 │                         ↑──────────────────────────────────(確定/キャンセル)
 ├─(Sensor View)──────→ SENSOR ──(BTN1 長押し)──→ MENU_NAV
 ├─(System)───────────→ MENU_NAV("/System")
 │   ├─(Info)─────────→ SYS_INFO ──(BTN1 長押し)──→ MENU_NAV
 │   └─(NVS Clear)────→ NVS_CLEAR_CONFIRM ──(Yes)──→ esp_restart()
 │                                          ──(No/長押し)──→ MENU_NAV
 ├─(Continuous)───────→ DONE_CONTINUOUS（継続モード移行）
 └─(Restart)──────────→ esp_restart()
```

各アクション画面の tick 関数は `service/menu.cpp` 内 static。  
ループ周期 50ms。

`BLE_REMOVE_CONFIRM → BLE_REMOVE` の遷移ではカーソル位置を復元する（`s_savedCursor`）。

---

## BLE 登録スキャンの仕組み

通常スキャン時、`SwitchBotCallback::onResult()` は `bleTargets.isTarget()` で未登録機器を弾く。
登録用スキャンでは `bleScanner.registrationMode = true` をセットしてこのフィルタをスキップする。

```cpp
// ble_scan.cpp onResult() 内
if (!bleScanner.registrationMode && !bleTargets.isTarget(addr.c_str())) return;
```

スキャン後、FreeRTOS キューから結果を取り出しアドレスで重複排除して表示する。

---

## 変更・追加ファイル一覧

| ファイル | 種別 | 内容 |
|----------|------|------|
| `service/menu.h/.cpp` | 変更 | path 方式の階層ナビ、NVS Clear 追加 |
| `device/button.h/.cpp` | 変更なし | デバウンス・長押し検出（`ButtonEvent`） |
| `device/oled.h/.cpp` | 変更なし | `oledShowMenu` / `oledShowMessage` / `oledShowConfirm` / `oledShowSensorData` |
| `device/ble_scan.h/.cpp` | 変更なし | `registrationMode` フラグ |
| `device/speaker.h/.cpp` | 変更なし | `bootStart` メロディ |
| `main.cpp` | 変更なし | `button.begin()` / BTN0 起動判定 |
