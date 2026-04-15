# AgentUI 実装計画

作成日: 2026-03-19
目的: Bedrock AgentCore を使った一般的な LLM チャット UI の試験実装
参考: スクショ（Lambdalith + Streaming/Buffered API-GW 分割 + CloudFront ルーティング）

---

## アーキテクチャ

```
Client
  → CloudFront (*.cloudfront.net)
      ├── /api/invoke*  → API-GW (HTTP API, Streaming) → Lambda (Hono) → AgentCore → Bedrock Claude
      ├── /api/history* → API-GW (HTTP API, Buffered)  → Lambda (Hono) → DynamoDB
      └── /*            → S3（チャット UI）
```

### ポイント
- Lambdalith パターン: 1つの Hono Lambda が全 API ルートを処理
- API-GW を Streaming / Buffered で2本に分け CloudFront でパス振り分け（1本に混在できないため）
- 認証: Hono ミドルウェアで `Authorization` ヘッダーの API キーを検証（tfvars で指定）
- AgentCore コード: S3 に Python コードを置くコード方式（コンテナ不要）

---

## ディレクトリ構成

```
agent/
├── main.tf
├── variables.tf
├── terraform.tfvars          # gitignore 対象
├── terraform.tfvars.example
├── agent.tf                  # 全インフラ定義
├── outputs.tf
├── backend.tfbackend         # key = "agent/terraform.tfstate"
├── backend.tfbackend.example
├── manage.ps1                # infra/ からコピー
├── lambda_src/
│   └── hono/                 # Hono アプリ（TypeScript）
│       ├── package.json
│       ├── tsconfig.json
│       └── src/
│           └── index.ts      # /api/invoke, /api/history
├── agentcore_src/            # AgentCore に載せる Python コード
│   └── index.py              # Bedrock Claude 呼び出し
└── web/
    └── index.html            # チャット UI（HTML + JS）
```

---

## Terraform リソース一覧（agent.tf）

| リソース | 内容 |
|---|---|
| `aws_s3_bucket.agentcore_code` | AgentCore コード（Python）を置く S3 |
| `aws_s3_object.agentcore_code` | agentcore_src/ をアップロード |
| `aws_iam_role.agentcore` | bedrock-agentcore.amazonaws.com の信頼関係 |
| `aws_iam_role_policy.agentcore_bedrock` | Bedrock モデル呼び出し権限 |
| `aws_bedrockagentcore_agent_runtime` | AgentCore ランタイム本体 |
| `aws_dynamodb_table.history` | 会話履歴（hash: userId, range: timestamp） |
| `aws_iam_role.lambda` | Lambda 実行ロール |
| `aws_iam_role_policy.lambda_dynamodb` | DynamoDB 読み書き権限 |
| `aws_iam_role_policy.lambda_agentcore` | AgentCore 呼び出し権限 |
| `aws_lambda_function.hono` | Hono アプリ（Node.js 22.x） |
| `aws_apigatewayv2_api.stream` | Streaming 用 API-GW |
| `aws_apigatewayv2_stage.stream` | $default ステージ |
| `aws_apigatewayv2_integration.stream` | Lambda プロキシ統合 |
| `aws_apigatewayv2_route.invoke` | POST /{proxy+} |
| `aws_lambda_permission.stream` | API-GW からの Lambda 呼び出し許可 |
| `aws_apigatewayv2_api.buffered` | Buffered 用 API-GW |
| `aws_apigatewayv2_stage.buffered` | $default ステージ |
| `aws_apigatewayv2_integration.buffered` | Lambda プロキシ統合 |
| `aws_apigatewayv2_route.history` | ANY /{proxy+} |
| `aws_lambda_permission.buffered` | API-GW からの Lambda 呼び出し許可 |
| `aws_s3_bucket.web` | チャット UI 配信用 S3 |
| `aws_s3_bucket_public_access_block.web` | パブリックアクセスブロック |
| `aws_cloudfront_origin_access_control.web` | OAC（S3 アクセス制御） |
| `aws_cloudfront_distribution.main` | パスで3オリジンに振り分け |
| `aws_s3_bucket_policy.web` | CloudFront OAC のみ許可 |
| `aws_s3_object.index_html` | チャット UI をアップロード |

---

## 変数（variables.tf）

| 変数名 | デフォルト | 説明 |
|---|---|---|
| `aws_region` | `ap-northeast-1` | AWSリージョン |
| `project` | `iot-monitor-agent` | リソース名プレフィックス |
| `api_key` | 必須 | Hono ミドルウェアで検証するキー |
| `bedrock_model_id` | `anthropic.claude-3-5-sonnet-20241022-v2:0` | 使用モデル |

---

## 実装タスク

### Terraform
- [ ] `agent/main.tf`
- [ ] `agent/variables.tf`
- [ ] `agent/terraform.tfvars.example`
- [ ] `agent/outputs.tf`
- [ ] `agent/agent.tf`
- [ ] `agent/backend.tfbackend`（infra/ からコピー、key 変更）
- [ ] `agent/backend.tfbackend.example`（infra/ からコピー）
- [ ] `agent/manage.ps1`（infra/ からコピー）

### Lambda（Hono）
- [ ] `agent/lambda_src/hono/package.json`
- [ ] `agent/lambda_src/hono/tsconfig.json`
- [ ] `agent/lambda_src/hono/src/index.ts`

### AgentCore コード
- [ ] `agent/agentcore_src/index.py`

### フロントエンド
- [ ] `agent/web/index.html`

---

## 進捗

| ステータス | 内容 |
|---|---|
| ✅ 完了 | 計画策定 |
| ✅ 完了 | Terraform ファイル群（main.tf, variables.tf, outputs.tf, agent.tf, tfvars.example） |
| ✅ 完了 | backend.tfbackend / backend.tfbackend.example / manage.ps1 |
| ✅ 完了 | Hono Lambda コード（src/index.ts, package.json, tsconfig.json） |
| ✅ 完了 | AgentCore Python コード（agentcore_src/index.py） |
| ✅ 完了 | チャット UI（web/index.html） |

## デプロイ手順

```powershell
# 1. Hono Lambda をビルド
cd agent/lambda_src/hono
npm install
npm run build
cd ../../..

# 2. tfvars を作成
cp agent/terraform.tfvars.example agent/terraform.tfvars
# agent/terraform.tfvars を編集して api_key を設定

# 3. Terraform apply
cd agent
.\manage.ps1 apply
```

## 既知の注意点・要確認事項

- AgentCore の SDK 呼び出し方法（`src/index.ts`）は暫定実装。デプロイ後のエラーで修正する可能性が高い
- Lambda IAM の `bedrock-agentcore:InvokeAgentRuntime` アクション名は未確認。エラー時に修正する
- AgentCore Python ハンドラーのシグネチャは公式ドキュメントで確認が必要
- `aws_bedrockagentcore_agent_runtime` の `code_artifact` ブロックの正確なスキーマは Terraform provider v6.28+ で確認すること
