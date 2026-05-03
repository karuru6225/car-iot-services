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
[メインメニュー]
  ├── BLE: Register    → [スキャン] → [結果一覧・登録]
  ├── BLE: Remove      → [登録済み一覧・削除確認]
  ├── Sensor View      → [センサーリアルタイム表示]
  ├── System Info      → [FW バージョン・デバイス ID]
  └── Restart          → esp_restart()
```

### 各画面

**メインメニュー**
```
Menu
──────────────────────
> BLE: Register
  BLE: Remove
  Sensor View
  System Info
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
BTN1 短押しで選択・登録 → NVS 保存 → メインに戻る。BTN1 長押しでキャンセル。

**BLE 削除**（登録済み一覧を兼ねる）
```
Remove (2)
──────────────────────
> aa:bb:cc:dd:ee:ff
  ff:ee:dd:cc:bb:aa
```
BTN1 短押しで確認画面へ。BTN1 長押しでメインに戻る。登録 0 件時は "No registered" を表示。

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

---

## ステートマシン

```
MAIN
 ├─(BLE:Register)──→ BLE_SCAN ──→ BLE_SCAN_RESULT ──(登録/キャンセル)──→ MAIN
 ├─(BLE:Remove)────→ BLE_REMOVE ──(BTN1 短押し)──→ BLE_REMOVE_CONFIRM
 │                       ↑──────────────────────────────────────────────(確定/キャンセル)
 ├─(Sensor View)───→ SENSOR ──(BTN1 長押し)──→ MAIN
 ├─(System Info)───→ SYS_INFO ──(BTN1 長押し)──→ MAIN
 └─(Restart)────────→ esp_restart()
```

各状態は `tick*()` 関数（`service/menu.cpp` 内 static）が担当する。
tick 関数は毎ループ呼ばれ、描画と入力処理を行い次状態を返す。ループ周期 50ms。

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
| `service/menu.h/.cpp` | 新規 | メニューステートマシン本体 |
| `device/button.h/.cpp` | 新規 | デバウンス・長押し検出（`ButtonEvent`） |
| `device/oled.h/.cpp` | 変更 | `oledShowMenu` / `oledShowMessage` / `oledShowConfirm` / `oledShowSensorData` 追加 |
| `device/ble_scan.h/.cpp` | 変更 | `registrationMode` フラグ追加 |
| `device/speaker.h/.cpp` | 変更 | `bootStart` メロディ追加、`playMelody` のセンチネル方式修正 |
| `main.cpp` | 変更 | `button.begin()` / `playMelody(bootStart)` を `setup()` に追加、BTN0 起動判定追加 |
