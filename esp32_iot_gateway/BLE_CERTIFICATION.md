# Bluetooth SIG QDID・認証調査ログ

esp32_iot_gatewayのBLE実装（`h2zero/NimBLE-Arduino`）がBluetooth SIGの認証（QDID）とどう関係するか、個人利用プロジェクトとしてどこまで気にする必要があるかを調べた記録。

**結論：自分の車にだけ載せる個人利用限定のため、QDID取得・公式ESP-IDF NimBLEへの差し替えは追求しない。現状のNimBLE-Arduinoのまま継続する。**

---

## QDIDとは何か

QDID（Qualified Design ID）はBluetooth SIGが発行する、Bluetooth製品を認証する際の識別子。完全なBluetooth製品は2つのサブシステムに分けて別々に認証される。

- **Controller Subsystem**：無線(RF)部分＋リンク層（ベースバンド・LLコントローラ）。ESP32-S3チップ内蔵のBLEハードウェア＋下位層ファームウェアが該当
- **Host Subsystem**：コントローラの上位のソフトウェアスタック（L2CAP, GAP, SMP, ATT/GATTなど）。コントローラとはHCI経由でやり取りする

両者を組み合わせて初めて完全なBluetooth製品として認証される。

## ESP32-S3-MINI-1のQDID構造

Bluetooth SIG Launch Studioの公開記録（Declaration ID D059143）で確認できた事実：

- ESP32-S3-MINI-1／MINI-1U／WROOM-1／WROOM-1U／WROOM-2／WROOM-2Uの6型番が**1つのDeclarationにまとめて登録**されている
- 同グループ内のESP32-S3-WROOM-1（QDID 182274）の個別Qualified Design詳細では、**Product Type = Controller Subsystem**、参照先QDID 141499（Host Subsystem = EspressifのESP-IDF NimBLEホスト実装）とセットで構成
- MINI-1も同一Declaration・同一チップ構成なので、Product Typeは同じくController Subsystemとして登録されていると判断できる

QDID系譜：

```
QDID 131934  Apache Mynewt NimBLE (Host)          ← Apache Software Foundation本家
     ↑ Combined Designs として参照
QDID 141499  Espressif ESP-IDF NimBLEホスト移植版 (Host Subsystem)
     ↑ h2zero/esp-nimble が nimble-1.5.0-idf ブランチを追跡
h2zero/NimBLE-Arduino（本プロジェクトが使用中）
```

## 現状の実装（h2zero/NimBLE-Arduino）の位置づけ

- [platformio.ini](platformio.ini) で `h2zero/NimBLE-Arduino@^1.4.0` を使用（[ble_peripheral.h](src/device/ble_peripheral.h), [ble_scan.h](src/device/ble_scan.h)）
- ソースコードの系譜としてはEspressif公式QDID 141499の直系フォークであり、無関係の未認証実装ではない
- ただし**Bluetooth SIGの認証は「同じ系譜のコード」だからといって自動的に引き継がれるものではなく、会社・製品ごとに登録されたDeclaration/QDIDの組み合わせでしか成立しない**。h2zeroやNimBLE-Arduino自体が独自のBluetooth SIG Declarationを持っているかは未確認（`qualification.bluetooth.com`はログイン必須の検索ポータルで外部から直接照会できなかった）

## 「認証取得可能な状態」に技術的に持っていく方法

技術層と行政（administrative）層は別問題。

### 技術層：可能

以下2点を満たせば「既に認証済みのサブシステムを未改変のまま組み合わせた」状態になる。

1. **Controller Subsystem**：ESP32-S3-MINI-1モジュールをEspressifの認証範囲内（リファレンスアンテナ設計等）で未改変のまま使う
2. **Host Subsystem**：h2zeroのNimBLE-Arduinoではなく、QDID 141499で認証を受けた**未改変の公式ESP-IDF NimBLEコンポーネント**に差し替える

ただし2は単純な設定変更では済まない。PlatformIOの`framework = espidf, arduino`混在モードを使えば既存のArduinoライブラリ群（TinyGSM, Adafruit SSD1306/GFX, ADS1X15等）はそのまま活かせるが、[ble_peripheral.cpp](src/device/ble_peripheral.cpp)（195行）・[ble_scan.cpp](src/device/ble_scan.cpp)（66行）は、h2zeroの便利なC++クラス（`NimBLEServer`, `NimBLECharacteristic`等）から、ESP-IDF公式の低レベルC API（GATT定義を静的構造体配列で記述、イベントenumで分岐する巨大コールバック等）への書き直しが必要で、実質2〜3倍のボリュームになる見込み。

### 行政層：ここに本当のハードルがある

技術要件を満たしても、正式にBluetooth SIGの認証を取得するには：

| 項目 | 内容 |
| --- | --- |
| Adopterメンバーシップ年会費 | $0（無料） |
| Product qualification fee | Adopter $12,000 / Contributing Adopter $8,000〜$12,000 / Associate $6,000 |
| 個人（法人を持たない個人）の加盟可否 | 公式ページに明記なし。多くの情報は企業単位が前提と示唆するが未確定。要Member Support確認 |
| 費用の単位 | 公式文書に明記なし。ただしEspressifのDeclaration D059143では6型番が1回の登録にまとまっていた実例あり＝1製品ごとの課金ではなさそうと推測できる（未確定） |

---

## QDID未認証で何が起きるか

### 起きうること

1. **Bluetoothロゴ・「Bluetooth」の名称使用ができない**（商標権侵害リスク）
2. **特許ライセンスの対象外という法的立場になる**：Bluetooth SIG会員企業間の特許相互ライセンス(PCLA)は認証完了が条件。個人利用で実際に問題化する可能性は低いが、法的には非ライセンス状態
3. 電波法（技適）・FCC/CE等の無線規制とは**別問題**。ESP32-S3-MINI-1モジュール自体は認証済み・未改変利用なら無関係

### 個人利用への影響

摘発経路（マーケットプレイス監視・税関報告、月300件以上）はいずれも**商業的な販売・流通**が前提。自分の車に載せて自己利用するだけなら、これらの監視網に引っかかる経路自体が存在しない。

### 実例調査

- **Bluetooth SIG Inc. v. FCA US LLC**（提訴〜控訴審決着 2022年、第9巡回区控訴裁判所）：FCA（Fiat Chrysler）が、サプライヤーからのSIG認証済みヘッドユニットを使いながら、FCA自身が完成車メーカーとして別途Declaration・費用支払いを行っていなかったため商標権侵害で提訴された。第9巡回区は「ファーストセール・ドクトリン」を理由に地裁判決を破棄・差し戻し、決着はグレー
- **Shenzhen Bluebird Hi-Tech.社の摘発**（2006年）：無許可でBluetoothロゴを使用しSIG認定試験をバイパスしたヘッドセット等20,000点以上を深圳市公安局が押収。ただし「丸ごと押収」型で、「途中でロゴだけ静かに消える」パターンとは異なる
- Bluetooth SIGの公式エンフォースメントプログラムでは**累計2,300件以上**の会員資格停止実績があるが、個別の企業名・事例は基本的に非公開。摘発は「全出品削除」または「会員登録・費用支払いの強制」が中心で、訴訟に発展するのはFCAのように相手が是正に応じない稀なケース
- 2023〜2025年の具体的な個別事例は公開情報からは確認できなかった（制度自体はEnforcement Program Policyが2025年8月5日付で更新されており、現在も運用中）
- 「ロゴが途中で消えた製品」から是正事例を逆算するのは、SIGの実運用（全出品削除・押収が中心で「ロゴだけ剥がして販売継続」という中間状態は想定されていない）とパターンが噛み合わず、現実的に困難

---

## 電波法・技適との関係（Bluetooth SIG QDIDとは別問題）

Bluetooth SIGのQDIDは業界団体の商標・特許ライセンス制度で、個人利用なら実務上関係ない（前述の通り）。一方で**電波法・技適は国の法律**であり、これは個人利用でも適用対象になるため別途確認した。

### モジュール自体の技適：確認済み

技適検索データベース（[giteki.lang-ship.com](https://giteki.lang-ship.com/201-230385)）で確認。

| 項目 | 内容 |
| --- | --- |
| モジュール名 | ESP32-S3-MINI-1 |
| 技適番号 | 201-230385 |
| 認証取得者 | Espressif Systems (Shanghai) Co., Ltd. |
| 工事設計認証日 | 2023年6月13日 |
| 認証方式 | 相互承認(MRA)による工事設計認証（認証機関：Kiwa Nederland B.V.） |
| 対応規格 | 2.4GHz帯小電力データ通信システム／高度化小電力データ通信システム（Wi-Fi/Bluetooth） |

### 組み込み製品として維持するための条件

技適はモジュール単体への認証なので、自社基板に組み込んだ完成品として合法であり続けるには以下が必要:

- モジュールを**未改変**で使うこと（ファーム側のRF出力・タイミングパラメータを認証範囲内に保つ）
- モジュール内蔵の**基板パターンアンテナをそのまま使う**こと（外部アンテナへの変更・給電線の改造をしない）
- モジュール周囲に指定された**アンテナ用キープアウトエリア**（銅箔・グラウンドプレーン・部品を置いてはいけない領域）を確保すること

これらが満たされていれば、総務省の2017年ルール改正（モジュール組み込み機器の届出簡略化）により、完成品を別途再認証する必要はない。

### 本プロジェクトでの実装状況

`esp32_iot_gateway/CONTEXT.md` によると、実機はKiCadプロジェクト `m5atom_power_adc` にESP32-S3-MINI-1-N8が基板直付け（外部アンテナ用`-U`品ではなく、モジュール内蔵アンテナを使用）。

- ファーム側：モジュール未改変で使用（本ドキュメント冒頭の通りNimBLE-Arduinoはアプリ層のみでRF層には手を入れていない）
- 基板側：アンテナのキープアウトエリアは設計者（プロジェクトオーナー）確認済み

以上より、**電波法・技適の観点では問題なしと判断**。QDID（Bluetooth SIG）の話とは完全に独立した結論であり、こちらは個人利用かどうかに関わらず満たしておくべき法的要件だが、現状クリアしている。

---

## 参考リンク

- [Espressif Bluetooth Launch Studio Listing (D059143)](https://launchstudio.bluetooth.com/ListingDetails/145952)
- [Dues and fees | Bluetooth Technology Website](https://www.bluetooth.com/fee-schedule/)
- [Join the SIG | Bluetooth Technology Website](https://www.bluetooth.com/develop-with-bluetooth/join/)
- [Bluetooth enforcement program](https://www.bluetooth.com/develop-with-bluetooth/qualify/bluetooth-enforcement-program/)
- [Trademark case: Bluetooth SIG Inc. v. FCA US LLC](https://legalblogs.wolterskluwer.com/trademark-blog/trademark-case-bluetooth-sig-inc-v-fca-us-llc-usa/)
- [Pairing Unsuccessful: Bluetooth and Fiat Denied Summary Judgment - Finnegan](https://www.finnegan.com/en/insights/blogs/incontestable/pairing-unsuccessful-bluetooth-and-fiat-denied-summary-judgment-in-suit-over-unauthorized-use-of-bluetooth-marks.html)
- [Bluetooth SIG factory raid nets 20,000 fakes - The Register (2006)](https://www.theregister.com/2006/09/26/bluetooth_sig_factory_raid/)
- [Is Using BLE in Personal, Non-Commercial Projects Actually Illegal? - Nordic DevZone](https://devzone.nordicsemi.com/f/nordic-q-a/120157/is-using-ble-in-personal-non-commercial-projects-actually-illegal-response-from-bluetooth-sig-included)
- [h2zero/NimBLE-Arduino](https://github.com/h2zero/NimBLE-Arduino)
- [h2zero/esp-nimble](https://github.com/h2zero/esp-nimble)
