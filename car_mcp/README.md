# car_mcp — 車両データの読み取り専用 MCP サーバ

個人用AIアシスタント mnemosyne（ネモ、`C:\Users\karuru\git_repos\ai`）が、会話の中で車のデータ
（バッテリー・トリップ・燃費）を読めるようにするための MCP サーバ。背景と設計上の制約は
リポジトリ直下の `HANDOFF_mnemosyne.md` を参照。

- **読み取り専用**。Shadow の書き換え・コマンド送信など車に作用する道具は置かない（許可リストで隠すのではなく、作らない）
- AWS へは専用の IAM ユーザー `iot-monitor-car-mcp`（`infra/car_mcp.tf`）で直接読みに行く。API Gateway は通らない
- 返り値は会話モデルがそのまま読む。値には単位と意味を添え、判定できること（燃費の妥当性、エンジン稼働の推定など）はコードで判定して渡す
- 時刻はすべて時差付きの JST（ISO 8601）

## 道具

名前はネモ側の許可リストにそのまま書かれるので、**一度決めたら変えない**。

| 道具 | 引数 | 返すもの | データ源 |
|---|---|---|---|
| `car_status` | なし | 最新のメイン/サブ電圧・サブ電流・積算Ah、最終受信時刻と経過時間、エンジン稼働/駐車中の推定、直近6件の推移、充電リレーの状態と自動充電の閾値 | Athena `sensor_data`（直近24時間）、IoT Shadow |
| `car_battery_history` | `start_date`, `end_date`（JSTの暦日 `YYYY-MM-DD`、最大92日） | 日ごとのメイン/サブ電圧の最低・最高、受信件数、サブバッテリーの充電量・放電量 | Athena `sensor_data`、S3 `rollup/` |
| `car_trips` | `start_date`, `end_date`（同上） | 期間内のトリップ一覧（出発/到着時刻・所要時間・距離・燃料・燃費）と、記録の限界・未分析の範囲の注意書き | S3 `trip-analysis/` |
| `car_trip_detail` | `trip_id`（`car_trips` が返したID） | 上記に加え LTFT/STFT 平均、触媒温度・ブースト圧の最高値、冷却水温の変化、OBD記録件数 | S3 `trip-analysis/` |

データが無い場合は「無い」と分かる文で返す。特にトリップは、スマートフォンが車内にあった走行だけが
記録されるため、「一覧に無い＝走っていない」ではないことを必ず添える。トリップ分析は管理画面から
手動で実行する方式なので、分析済みの範囲より後の走行はまだ一覧に出ない（その旨も返す）。

### 判定に使っている値

| 値 | 場所 | 根拠 |
|---|---|---|
| エンジン稼働の推定: メイン電圧 13.2V 以上 | `carmcp/battery.py` `ENGINE_RUNNING_MIN_V` | 鉛バッテリーの一般的な目安（オルタネーター発電中の電圧帯）。**この車両の実測では未検証**。サブ→メイン充電中は推定しない |
| 計測が古いとみなす: 900秒 | `carmcp/battery.py` `STALE_AFTER_SEC` | DEEP_SLEEP の送信間隔 300秒の3回分 |
| 燃費を参考外とする: 走行 1km 未満 | `carmcp/trips.py` `ECONOMY_MIN_DISTANCE_KM` | 短距離は燃料流量の積分誤差が支配的 |
| 燃費をありえない値とする: 40km/L 超 | `carmcp/trips.py` `ECONOMY_MAX_PLAUSIBLE_KM_L` | 通信断で燃料が過小積算された実例（372km/L）がある |

## 接続の仕方（ネモ側で要る4点）

| 項目 | 値 |
|---|---|
| 起動方法 | このディレクトリの `Dockerfile` からビルド（既定ターゲット）。`python -m carmcp.server` で起動する |
| ポート・パス | コンテナ内 `8000`、MCP は `http://<サービス名>:8000/mcp`（Streamable HTTP、ステートレス）。ヘルスチェックは `GET /healthz`（認証なし） |
| ツール名 | `car_status`, `car_battery_history`, `car_trips`, `car_trip_detail` |
| トークン | `Authorization: Bearer <CAR_MCP_TOKEN>`。サーバとネモの両方に同じ値を渡す |

ネモの `docker/docker-compose.yml` に足す例（パスや変数名はネモ側に合わせる）:

```yaml
  car-mcp:
    build: <car-iot-servicesのパス>/car_mcp
    restart: unless-stopped
    expose: ["8000"]              # ホストへは出さない
    environment:
      CAR_DEVICE_ID: esp32-gw-xxxxxxxxxxxx
      CAR_S3_BUCKET: iot-monitor-<AWSアカウントID>
      CAR_IOT_ENDPOINT: xxxxxxxxxxxxxx-ats.iot.ap-northeast-1.amazonaws.com
      CAR_MCP_TOKEN: ${CAR_MCP_TOKEN}
      AWS_ACCESS_KEY_ID: ${CAR_AWS_ACCESS_KEY_ID}
      AWS_SECRET_ACCESS_KEY: ${CAR_AWS_SECRET_ACCESS_KEY}
      AWS_DEFAULT_REGION: ap-northeast-1

  orchestrator:
    environment:
      CAR_MCP_URL: http://car-mcp:8000/mcp
      CAR_MCP_TOKEN: ${CAR_MCP_TOKEN}
      CAR_TOOLS: car_status,car_battery_history,car_trips,car_trip_detail
    depends_on:
      car-mcp:
        condition: service_healthy
```

ネモの `@modelcontextprotocol/sdk` 1.30.0 の `StreamableHTTPClientTransport` から、接続・認証拒否・
ツール一覧・呼び出しが通ることを確認済み。

## 環境変数

| 変数 | 必須 | 既定値 | 内容 |
|---|---|---|---|
| `CAR_DEVICE_ID` | ✓ | | IoT Thing 名（`esp32-gw-{MAC12桁}`） |
| `CAR_S3_BUCKET` | ✓ | | データバケット（`terraform output s3_bucket`） |
| `CAR_IOT_ENDPOINT` | ✓ | | IoT のデータエンドポイント（`terraform output iot_endpoint`） |
| `CAR_MCP_TOKEN` | ✓ | | Bearer トークン（32文字以上） |
| `CAR_OBD_DEVICE_ID` | | `CAR_DEVICE_ID` から導出 | OBD・トリップの識別子（`car-iot-{MAC上位6桁}`） |
| `CAR_ATHENA_DATABASE` | | `iot_monitor` | |
| `CAR_ATHENA_WORKGROUP` | | `iot-monitor` | |
| `CAR_EXPOSE_LOCATION` | | `false` | `true` のときだけトリップの出発/到着の地名を返す。座標はどの設定でも返さない |
| `CAR_MCP_ALLOWED_HOSTS` | | `car-mcp:*,localhost:*,127.0.0.1:*` | 受け付ける Host ヘッダ（DNSリバインディング対策）。compose のサービス名を変えたらここも変える |
| `CAR_MCP_PORT` | | `8000` | |
| `AWS_ACCESS_KEY_ID` / `AWS_SECRET_ACCESS_KEY` / `AWS_DEFAULT_REGION` | ✓ | | 下記の IAM ユーザーのキー |

## 初回の準備

1. `infra/` で `terraform apply` し、IAM ユーザー `iot-monitor-car-mcp` を作る
2. アクセスキーを CLI で発行する（Terraform で作ると秘密鍵が tfstate に平文で残るため）

   ```bash
   aws iam create-access-key --user-name iot-monitor-car-mcp
   ```

3. Bearer トークンを作る

   ```bash
   python -c "import secrets; print(secrets.token_urlsafe(32))"
   ```

4. キーとトークンはネモ側の `.env` 等に置き、リポジトリにはコミットしない

キーを入れ替えるときは、新しいキーを発行 → ネモ側を差し替え → 古いキーを `aws iam delete-access-key` で消す。

## テスト

AWS には繋がない。AWS への読み取りは `carmcp/aws.py` の `AwsGateway` に集めてあり、テストでは同じメソッドを
持つ偽物（`tests/conftest.py` の `FakeGateway`）に差し替える。`tests/test_server.py` は実際に uvicorn を立てて
HTTP で叩き、認証・Host の検査・道具の一覧と呼び出しを確かめる。

```bash
# car_mcp/ で実行
docker build --target test -t car-mcp-test . && docker run --rm car-mcp-test
```

## 構成

```text
car_mcp/
├── Dockerfile            base → test / runtime（既定）の多段ビルド
├── requirements.txt      mcp（MCP公式Python SDK 2.x）, boto3
├── carmcp/
│   ├── server.py         道具の登録・Bearer認証・/healthz・起動
│   ├── battery.py        car_status / car_battery_history
│   ├── trips.py          car_trips / car_trip_detail
│   ├── aws.py            Athena・S3・Shadowの読み取り（AwsGateway）
│   ├── config.py         環境変数の読み込みと検証
│   └── timeutil.py       JST変換・期間の検証
└── tests/
```

ディレクトリ名を `mcp/` にしていないのは、SDK のパッケージ名 `mcp` と衝突して `import mcp` が
自分のディレクトリを読んでしまうのを避けるため。
