# SIM7080 Series AT Command Reference (SSL / FileSystem / MQTT)

SIM7080シリーズにおけるファイル操作、SSL設定、およびMQTT通信の実装向けリファレンスです。

---

## 1. FileSystem (ファイルシステム)

内蔵フラッシュメモリへのアクセス手順です。SSL証明書の配置やログ保存等に使用します。

### 基本操作フロー

1. `AT+CFSINIT` でバッファを初期化。
2. `AT+CFSWFILE` や `AT+CFSRFILE` で操作。
3. `AT+CFSTERM` でリソースを解放。

### 主要コマンド

| コマンド | フォーマット | パラメータ詳細 |
| :--- | :--- | :--- |
| **初期化** | `AT+CFSINIT` | 戻り値: `OK` (操作前に必須) |
| **終了** | `AT+CFSTERM` | 戻り値: `OK` (操作後に必須) |
| **ファイル書込** | `AT+CFSWFILE=<dir>,<name>,<mode>,<size>,<time>` | `<dir>`: 0(/custapp/), 3(/customer/)<br>`<mode>`: 0(上書き), 1(追記)<br>`<time>`: タイムアウト(ms) |
| **ファイル読込** | `AT+CFSRFILE=<dir>,<name>,<mode>,<size>,<pos>` | `<mode>`: 0(最初から), 1(指定位置から)<br>`<pos>`: 開始オフセット |
| **ファイル削除** | `AT+CFSDFILE=<dir>,<name>` | ファイルを削除します |
| **サイズ確認** | `AT+CFSGFIS=<dir>,<name>` | ファイルが存在すればサイズを返します |

---

## 2. SSL (セキュア通信設定)

MQTTやHTTPで暗号化通信を行うための設定です。

### SSLプロファイル設定 (`AT+CSSLCFG`)

SSLコンテキスト（`<ctx>` は通常 0 を使用）に対して設定します。

| 設定項目 | コマンド例 | 説明 |
| :--- | :--- | :--- |
| **SSLバージョン** | `AT+CSSLCFG="sslversion",0,3` | `1`:TLS1.0, `2`:TLS1.1, `3`:TLS1.2, `4`:All |
| **認証モード** | `AT+CSSLCFG="authmode",0,<n>` | `0`:認証なし, `1`:サーバー認証, `2`:相互認証 |
| **CA証明書指定** | `AT+CSSLCFG="cacert",0,"ca.crt"` | FileSystem内のファイル名を指定 |
| **クライアント証明書** | `AT+CSSLCFG="clientcert",0,"cli.crt"` | 相互認証用 |
| **クライアント鍵** | `AT+CSSLCFG="clientkey",0,"cli.key"` | 相互認証用 |
| **SNI設定** | `AT+CSSLCFG="sni",0,"example.com"` | ホスト名ベースの証明書検証用 |
| **時刻無視** | `AT+CSSLCFG="ignorentctime",0,1` | `1`:証明書の有効期限検証でRTCを無視 |

---

## 3. MQTT (パブリッシュ/サブスクライブ)

MQTTプロトコルの設定とシーケンスです。

### 接続設定 (`AT+SMCONF`)

接続前に以下のパラメータをセットします。

| パラメータ | コマンド | 例 |
| :--- | :--- | :--- |
| **ホスト名/ポート** | `"URL"` | `AT+SMCONF="URL","example.com",1883` |
| **クライアントID** | `"CLIENTID"` | `AT+SMCONF="CLIENTID","device001"` |
| **キープアライブ** | `"KEEPALIVE"` | `AT+SMCONF="KEEPALIVE",60` |
| **ユーザー名** | `"USERNAME"` | `AT+SMCONF="USERNAME","user1"` |
| **パスワード** | `"PASSWORD"` | `AT+SMCONF="PASSWORD","pass123"` |
| **クリーンセッション** | `"CLEANSS"` | `0` または `1` |

### 通信操作

1. **SSL紐付け** (SSLを使用する場合):
   `AT+SMSSL=1,0` (SSL有効化, コンテキストID=0)
2. **サーバー接続**:
   `AT+SMCONN`
   - 成功時 `OK`。接続エラー時は `ERROR`。
3. **トピック購読 (Subscribe)**:
   `AT+SMSUB="topic/test",1` (トピック, QoS)
   - 受信時通知: `+SMSUB: "topic/test","message_content"`
4. **メッセージ発行 (Publish)**:
   `AT+SMPUB="topic/test",5,1,0` (トピック, 長さ, QoS, Retain)
   - コマンド送出後、`>` プロンプトが表示されたら 5バイトのデータを送信。
5. **接続解除**:
   `AT+SMDISC`

---

## 実装上のヒント

- **エラー処理**: MQTT操作中に `+SMSTATE: 0` (切断) が通知された場合、再度 `AT+SMCONN` からリトライするロジックが必要です。
- **データ送信**: `AT+SMPUB` や `AT+CFSWFILE` の後は、モジュールからの `>` を待ってからデータを送信してください。
- **証明書のロード**: 証明書をFSに書き込んだ後、`AT+CSSLCFG="convert",...` を実行する必要がある場合があります（PEMからDERへの変換など）。
