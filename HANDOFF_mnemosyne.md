# 引き継ぎ: 個人用AIアシスタント mnemosyne（ネモ）への車両データ連携

対象リポジトリ: `karuru6225/car-iot-services`
連携先: `C:\Users\karuru\git_repos\ai`（mnemosyne。以下「ネモ」）
背景: ネモとの会話で車のデータ（バッテリー・トリップ・燃費）を使えるようにする。ネモ側のセッションで
連携方法を検討し、**車側の実装はこのリポジトリで行う**と決めた。この文書はその検討結果の引き継ぎ。

---

## 0. 現状まとめ

**①を実装済み**（API Gateway → Lambda `iot-monitor-car-mcp`、ブランチ `feat/car-mcp`）。
`terraform apply` と実AWSでの動作確認、ネモ側への組み込み（④）はまだ。
ネモ側で接続を始めるときに要ること（URL・認証・ツール名・トークンの取り方）は **8章** にまとめた。

| 段階 | 内容 | 担当 |
|---|---|---|
| ① | 読み取り専用の MCP サーバを作る（会話中に車のデータを引ける状態にする）— **実装済み** | このリポジトリ |
| ② | 「放っておけない変化」を離散イベントとして出す口を①に足す | このリポジトリ |
| ③ | トリップ分析を自動実行にする（任意。②のトリップ終了に要る） | このリポジトリ |
| ④ | ネモの compose への組み込み、orchestrator の許可リスト、watcher の `car` アダプタ | ネモ側（後で別セッション） |

①だけでも「昨日の燃費どうだった？」に答えられるようになるので、①から始める。

---

## 1. 連携の前提（ネモ側の制約）

ネモ側の設計上の決まりのうち、この連携に効くものだけを書く（出典はネモの `mnemosyne-architecture.md` 7章）。

- **車のデータをネモの長期記憶へ直接書き込まない**。ネモでは、会話で話題になったものだけが睡眠処理を経て
  記憶になる（「注意を向けなかったものは記憶に残らない」）。5分ごとの値やトリップを直接流し込むと、
  ノイズで記憶が汚れる。**このリポジトリ側から記憶への書き込み口を作る必要は無い**
- **ネモに見せるのは読み取りの道具だけ**。ネモの orchestrator は、許可リストに載せた名前のツールだけを
  会話モデルに渡す。shadow の書き換え・コマンド送信・デバイス管理など、車に作用する道具は MCP サーバに**置かない**
  （置いた上で許可リストで隠すのではなく、そもそも作らない）
- **顕著性（割り込む価値）の点数はネモ側が持つ**。このリポジトリが出すのは「何が起きたか」と種類（`urgent`/`notice`）
  までで、数値の点数は出さない。ネモ側で種類ごとに固定値（urgent 0.8 / notice 0.55）を当てる
- **Claude API へ送られる**。会話モデルはクラウドの Claude なので、ツールの返り値はそのまま外に出る。
  位置情報の扱いは5章を参照

---

## 2. ネモが受け取れる形（インターフェース）

ネモの orchestrator は、Home Assistant の MCP サーバと同じ方法で接続する（`apps/orchestrator/src/index.ts` の
Home Assistant 接続部分、`apps/orchestrator/src/mcp-client.ts`）。

- **トランスポート**: MCP の **Streamable HTTP**（`@modelcontextprotocol/sdk` の `StreamableHTTPClientTransport`）。
  SSE だけのサーバや stdio には繋がらない
- **認証**: `Authorization: Bearer <token>` を付けられる。`StreamableHTTPClientTransport` に `fetch` を渡せば
  リクエストごとにトークンを差し替えられる（期限のあるトークンを使える。8章）
- **起動順**: ネモは起動時に2回試して繋がらなければ、車の道具が無い状態で動き続ける（落ちない）
- **配置**: 当初はネモの compose にサービスとして加える案だったが、AWS を読むための長期アクセスキーを手で
  発行して渡す必要が出るため、**API Gateway → Lambda に載せる形に変えた**（6章）。ネモは URL を向けるだけ
- **ツール名**: ネモ側の許可リスト（環境変数、カンマ区切り）にそのまま書くので、**一度決めたら変えない**。
  `car_` を接頭辞にする

### ① 会話用のツール（案）

| ツール | 返すもの | 主なデータ源 |
|---|---|---|
| `car_status` | 最新のメイン/サブ電圧・サブ電流・積算Ah、最終受信時刻、充電状態など shadow の現在値 | `sensor_data` の直近の行、IoT の Shadow |
| `car_battery_history` | 期間を指定した日次の充電Ah・放電Ah、電圧の最小/最大 | `battery_rollup`、`sensor_data` |
| `car_trips` | 期間を指定したトリップ一覧（開始/終了時刻・距離・燃費・所要時間） | `trip-analysis/` の結果 |
| `car_trip_detail` | 1トリップの詳細（LTFT/STFT 平均、触媒温度・ブーストの最大値など） | 同上 |

返り値について:

- **単位と意味を返り値に含める**。会話モデルは列名から推測するので、`main_v: 12.41` ではなく
  「メインバッテリー電圧 12.41V（駐車中の静止電圧）」のように、読んで誤解しない形にする。
  trip_analysis で AI ナレーティブを撤去した件（71km/h を高速道路と誤判定した件、372km/L を良好と評価した件）と
  同じ問題が起きうる。**判定できることはコード側で判定して、事実として渡す**
- **データが無いことを明示する**。OBD はスマホが車内にあるときしか届かないので、「期間中のデータ無し」と
  「走っていない」は区別して返す
- **期間の上限を決めておく**。Athena のスキャン量と応答時間を抑えるため
- **時刻は時差付きの ISO 8601（JST）で返す**。UTC のエポック秒だと、会話モデルが日付を取り違える

### ② イベント用のツール（案）

ネモの watcher（常駐して「知らせるべき変化」を拾う部分）が定期的に呼ぶ。

```
car_events(since: ISO 8601) -> [{
  id: string,          // 安定した識別子。同じ出来事には同じ id（ネモ側で重複を抑えるのに使う）
  kind: 'urgent' | 'notice',
  type: string,        // 'battery_low' | 'battery_draining' | 'device_silent' | 'trip_ended' など
  occurred_at: string, // ISO 8601
  digest: string,      // 一文の要約。会話モデルがこれを読んで話しかけるかを決める
}]
```

- **判定の規則はこのリポジトリ側に置く**。電圧の閾値や「駐車中」の判定は車のドメイン知識なので、ネモ側に持ち込むと二重管理になる
- **可変なステートを持たずに毎回計算し直す**方式を推奨する。trip_analysis で DynamoDB の read-modify-write の競合を避けたのと同じ理由で、
  直近の行から「閾値を下回った状態が N 分続いているか」をその都度計算すれば、ステートの競合が構造的に起きない
- イベントの候補と種類の案:

| type | kind | 条件の案（値は未定） | 理由 |
|---|---|---|---|
| `battery_low` | urgent | 駐車中にメイン電圧が閾値を下回った状態が N 分続く | 放置すると始動できなくなる。CONTEXT.md の未着手 TODO「バッテリー上がりアラート」をこれで兼ねられる |
| `battery_draining` | notice | 電圧の低下傾向、サブ→メインへの逆流（60〜70mA を観測済み）が続く | 急がないが気にしている事象 |
| `device_silent` | notice | 送信間隔（DEEP_SLEEP なら300秒）から見て想定を超えて途絶えている | 故障・圏外・電源断 |
| `trip_ended` | notice | トリップ分析で新しいトリップが確定した | 割り込むほどではない。次の会話の話題として使う |

- **閾値は実データを再生して決める**。ネモの Home Assistant 連携では、最初に決めた値で過去7日分を再生して
  見逃しと誤報を数えてから確定した。同じやり方を勧める
- **検知の遅れ**: バッテリーは LTE 経由で最短5分間隔なので、それ以上速くはならない。OBD はスマホ経由なので、
  リアルタイムの判定には使わない

---

## 3. AWS へのアクセス

MCP サーバは Lambda で動き、**Lambda の実行ロール**で AWS を読む。ネモ側に AWS の認証情報は置かない。
実行ロールに付けているのは読み取りだけ（`infra/car_mcp.tf`）:

- Athena: ワークグループ `iot-monitor` でのクエリ実行と結果の取得
- Glue: `iot_monitor` データベースと `sensor_data` テーブルの定義の読み取り
- S3: データバケットの `raw/`・`rollup/`・`trip-analysis/` の読み取りと、Athena 結果の置き場（`athena-results/`）への書き込み
- IoT: 対象デバイス1台の `iot:GetThingShadow` だけ

ネモから Lambda への呼び出しは、Cognito の client_credentials で取ったトークンで認証する（8章）。

---

## 4. トリップ分析の自動化（③）

`trip_ended` を出すには、トリップが自動で確定している必要がある。今は管理画面から手動実行したときしか動かない
（HANDOFF_trip_analysis.md）。

- EventBridge Scheduler で定期実行する、が素直な形。旧設計（1分ごとのポーリング＋DynamoDB）を廃止した経緯があるので、
  **S3 の結果から分析済み範囲を逆算する今の方式のまま、呼び出しだけを定期化する**。頻度は1時間〜1日で十分
- 自動化しない場合は、`car_events` の中で「分析済みより後の OBD データが一定時間途切れている」ことから
  トリップ終了を推定する方法もある。ただし分割のロジックが二重になる

---

## 5. 位置情報

OBD の行に付いている lat/lon と、逆ジオコーディングした地名（`start_location`/`end_location`）は、返り値に入れた時点で
Claude API へ送られる。

- **座標は返さない**
- 地名は渡してよい（6章）。ただし Terraform 変数 `car_mcp_expose_location`（既定 `false`）で切り替え、既定は「返さない」
- 返さない場合でも、距離・所要時間は返す

---

## 6. 決定事項と未決定事項

決定済み（2026-09-19）:

1. **AWS の認証と配置**: MCP サーバを API Gateway → Lambda に載せ、Lambda の実行ロールで読む。
   最初はネモの compose に常駐させて読み取り専用の IAM ユーザーのアクセスキーを渡す形で作ったが、キーを手で発行して
   渡す作業が要り、長期のキーも残るため作り直した。調べた範囲でも、AWS 自身が勧めるリモート MCP の形は
   「Lambda＋API Gateway＋OAuth（Cognito）」だった（awslabs/run-model-context-protocol-servers-with-aws-lambda）。
   AWS 側に新しい口は開くが、既存の API Gateway にルートを1本足すだけで、専用の JWT Authorizer とスコープで守る
2. **位置情報**: 地名は渡してよい。ただし 5章のとおり切り替え式にし、既定は「返さない」
3. **実装言語と依存**: Python。**MCP の SDK は使わず、必要な範囲（initialize / ping / tools/list / tools/call）を手書き**した。
   SDK を Lambda に同梱すると pydantic_core 等のネイティブ依存を Linux 向けにビルドする必要があり、他の Lambda と同じ
   `archive_file` だけのデプロイができなくなるため。新しい依存は無い
4. **置き場所**: `infra/lambda_src/car_mcp/`（他の Lambda と同じ場所）

未決定:

5. **イベントの閾値**: 2章の表の値。②に着手するときに、過去データの再生結果を見てから決める

---

## 7. ネモ側で後からやること（参考。このリポジトリの作業ではない）

- 環境変数 `CAR_MCP_URL`・`CAR_MCP_TOKEN_ENDPOINT`・`CAR_MCP_CLIENT_ID`・`CAR_MCP_CLIENT_SECRET`・`CAR_TOOLS`（許可リスト）を追加。
  値は 8章のとおり `terraform output` で取り出せる
- orchestrator で Home Assistant と同じように接続し、許可リストで絞る。ただし Home Assistant と違ってトークンに期限（1時間）が
  あるので、固定の `headers` ではなく、トークンを取り直す `fetch` を渡す（8章のコード）
- watcher に `car` アダプタを足す。`car_events` を定期的に呼んで、`kind` を固定の顕著性へ対応させるだけの薄いアダプタにする
  （規則で判定する。LLM は使わない）
- ネモの `docs/manual/`（会話モデルが自分の機能を説明するための文書）に車の道具を追記する

---

## 8. ①の実装: 接続のしかたと道具の中身

### 接続情報

`infra/` で `terraform apply` した後、次で取り出す（手で発行するものは無い）:

```bash
terraform output -raw car_mcp_url             # https://xxxx.execute-api.ap-northeast-1.amazonaws.com/mcp
terraform output -raw car_mcp_token_endpoint  # https://iot-monitor-<アカウントID>.auth.ap-northeast-1.amazoncognito.com/oauth2/token
terraform output -raw car_mcp_scope           # car-mcp/read
terraform output -raw car_mcp_client_id
terraform output -raw car_mcp_client_secret
```

| 項目 | 値 |
|---|---|
| トランスポート | Streamable HTTP。**ステートレス**（セッションIDを発行しない）で、応答は常に `application/json`。GET/DELETE には 405 を返す |
| 認証 | Cognito の client_credentials で取ったアクセストークンを `Authorization: Bearer` に付ける。有効期限は1時間 |
| ツール名 | `car_status`, `car_battery_history`, `car_trips`, `car_trip_detail`（**変えない**） |
| プロトコル版 | `2025-11-25` / `2025-06-18` / `2025-03-26` に対応。ネモの SDK 1.30.0 とは `2025-11-25` で合意する |

Web・モバイル用のトークンでは `/mcp` を呼べず、このクライアントのトークンでは `/data` 等の既存ルートを呼べない
（Authorizer をネモ専用に分けてある）。

### ネモ側でのトークンの付け方

`connectMcpTools` は固定の `headers` を渡す作りなので、期限切れに備えて `fetch` を差し替える。
リクエストごとに `fetch` でトークンを付ける形で、ネモの SDK 1.30.0 から接続・一覧・呼び出しが通ることを確認済み
（Cognito と API Gateway の代わりに固定トークンで通す検証用の中継で確認。実 Cognito では未確認）:

```ts
const tokenEndpoint = process.env.CAR_MCP_TOKEN_ENDPOINT!;
const basic = Buffer.from(`${process.env.CAR_MCP_CLIENT_ID}:${process.env.CAR_MCP_CLIENT_SECRET}`).toString('base64');
let cached: { token: string; expiresAt: number } | undefined;

async function carToken(): Promise<string> {
  if (cached && Date.now() < cached.expiresAt - 60_000) return cached.token;
  const res = await fetch(tokenEndpoint, {
    method: 'POST',
    headers: { Authorization: `Basic ${basic}`, 'Content-Type': 'application/x-www-form-urlencoded' },
    body: new URLSearchParams({ grant_type: 'client_credentials', scope: 'car-mcp/read' }),
  });
  if (!res.ok) throw new Error(`Cognitoのトークン取得に失敗: ${res.status}`);
  const body = (await res.json()) as { access_token: string; expires_in: number };
  cached = { token: body.access_token, expiresAt: Date.now() + body.expires_in * 1000 };
  return cached.token;
}

const transport = new StreamableHTTPClientTransport(new URL(process.env.CAR_MCP_URL!), {
  fetch: async (url, init = {}) => {
    const headers = new Headers(init.headers);
    headers.set('Authorization', `Bearer ${await carToken()}`);
    return fetch(url, { ...init, headers });
  },
});
```

トークンのキャッシュは必須。Cognito の M2M はトークン要求の回数で課金される（1000件あたり $0.00225。
1時間に1回なら月 $0.002 程度）。

### 道具の中身

| 道具 | 引数 | 返すもの | データ源 |
|---|---|---|---|
| `car_status` | なし | 最新のメイン/サブ電圧・サブ電流・積算Ah、最終受信時刻と経過時間、エンジン稼働/駐車中の推定、直近6件の推移、充電リレーの状態と自動充電の閾値 | Athena `sensor_data`（直近24時間）、IoT Shadow |
| `car_battery_history` | `start_date`, `end_date`（JSTの暦日 `YYYY-MM-DD`、最大92日） | 日ごとのメイン/サブ電圧の最低・最高、受信件数、サブバッテリーの充電量・放電量 | Athena `sensor_data`、S3 `rollup/` |
| `car_trips` | `start_date`, `end_date`（同上） | 期間内のトリップ一覧（出発/到着時刻・所要時間・距離・燃料・燃費）と、記録の限界・未分析の範囲の注意書き | S3 `trip-analysis/` |
| `car_trip_detail` | `trip_id`（`car_trips` が返したID） | 上記に加え LTFT/STFT 平均、触媒温度・ブースト圧の最高値、冷却水温の変化、OBD記録件数 | S3 `trip-analysis/` |

返り値は日本語のキーと、単位・意味を添えた値の JSON テキスト。データが無い日は「受信データが無い」と明示し、
トリップには「一覧に無いことは走っていないことを意味しない」「分析は○○までしか済んでいない」を必ず添える。
引数の誤りや AWS 側の失敗は、会話モデルが読める文で `isError: true` の結果として返す。

判定に使っている値（しきい値・期間の上限などは **`infra/lambda_src/car_mcp/carmcp/thresholds.py` の1ファイルにまとめてある**。変えるときはそこだけを直す）:

| 値 | 定数 | 根拠 |
|---|---|---|
| エンジン稼働の推定: メイン電圧 13.2V 以上 | `ENGINE_RUNNING_MIN_V` | 鉛バッテリーの一般的な目安（オルタネーター発電中の電圧帯）。**この車両の実測では未検証**。サブ→メイン充電中は推定しない |
| 計測が古いとみなす: 900秒 | `STALE_AFTER_SEC` | DEEP_SLEEP の送信間隔 300秒の3回分 |
| 燃費を参考外とする: 走行 1km 未満 | `ECONOMY_MIN_DISTANCE_KM` | 短距離は燃料流量の積分誤差が支配的 |
| 燃費をありえない値とする: 40km/L 超 | `ECONOMY_MAX_PLAUSIBLE_KM_L` | 通信断で燃料が過小積算された実例（372km/L）がある |

### コードとテスト

- `infra/lambda_src/car_mcp/index.py` — Lambda の入口（スコープ確認・HTTPメソッド・道具の定義）
- `carmcp/protocol.py` — MCP の JSON-RPC 処理 / `battery.py`・`trips.py` — 道具の中身 / `aws.py` — Athena・S3・Shadow の読み取り
- テストは他の Lambda と同じ Docker の仕組みで AWS に繋がずに回る（`infra/lambda_src/TESTING.md`）。AWS への読み取りは
  `AwsGateway` に集めてあり、テストでは `tests/car_mcp_fakes.py` の偽物に差し替える

```bash
# infra/lambda_src/ で実行
docker compose -f docker-compose.test.yml run --build --rm test pytest car_mcp -v
```
