# Amazon Bedrock AgentCore メモ

調査日: 2026-03-19
目的: car-iot-services の `agent/` ディレクトリで AgentCore を含む AI エージェント基盤を構築するための参考資料

---

## AgentCore とは

AWS が提供するエンタープライズグレードの AI エージェント実行プラットフォーム。インフラ管理不要でエージェントをデプロイ・スケール・監視できる。2025年 GA。

### 主要コンポーネント

| コンポーネント | 役割 |
|---|---|
| **Agent Runtime** | コンテナ（ECR）またはコード（S3）としてエージェントをホスト |
| **Gateway** | Lambda 関数・API を MCP（Model Context Protocol）互換ツールとして公開 |
| **Memory** | エージェントの会話記憶を永続化（7〜365 日） |
| **Credential Provider** | API キー / OAuth2 認証情報の管理 |
| **Browser / Code Interpreter** | ウェブ操作・コード実行のマネージドセッション |

---

## 対応リージョン

東京（`ap-northeast-1`）含む9リージョンで利用可能。

- us-east-1, us-east-2, us-west-2
- ap-south-1, ap-southeast-1, ap-southeast-2, **ap-northeast-1**
- eu-west-1, eu-central-1

---

## Terraform サポート状況

**AWS Provider v6.28.0 以降**で `aws_bedrockagentcore_*` リソースが追加済み。2026年3月時点でアクティブ開発中のため、使用する provider バージョンのリリースノートを都度確認すること。

### 実装済みリソース

| リソース名 | 用途 |
|---|---|
| `aws_bedrockagentcore_agent_runtime` | エージェント実行環境 |
| `aws_bedrockagentcore_gateway` | MCP ゲートウェイ |
| `aws_bedrockagentcore_gateway_target` | ゲートウェイのターゲット（Lambda/API） |
| `aws_bedrockagentcore_memory` | メモリストア |
| `aws_bedrockagentcore_memory_strategy` | メモリの抽出・保存戦略 |
| `aws_bedrockagentcore_api_key_credential_provider` | API キー認証情報プロバイダー |
| `aws_bedrockagentcore_oauth2_credential_provider` | OAuth2 認証情報プロバイダー |
| `aws_bedrockagentcore_resource_policy` | リソースポリシー |

`aws_bedrockagentcore_browser_profile` は未実装（issue 起票済み）。

---

## Agent Runtime の作成

エージェントコードのデプロイ方式は **コンテナ（ECR）** または **コード（S3）** の2択。

### コンテナ方式（ECR）

```hcl
resource "aws_bedrockagentcore_agent_runtime" "example" {
  agent_runtime_name = "my-agent"
  role_arn           = aws_iam_role.agentcore.arn

  agent_runtime_artifact {
    container_configuration {
      container_uri = "123456789.dkr.ecr.ap-northeast-1.amazonaws.com/my-agent:latest"
    }
  }

  network_configuration {
    network_mode = "PUBLIC"  # または "VPC"
  }

  environment_variables = {
    KEY = "value"
  }
}
```

### IAM ロール（AgentCore 用）

AgentCore が ECR からイメージを pull するためのロール。

```hcl
resource "aws_iam_role" "agentcore" {
  name = "agentcore-runtime-role"
  assume_role_policy = jsonencode({
    Version = "2012-10-17"
    Statement = [{
      Effect    = "Allow"
      Principal = { Service = "bedrock-agentcore.amazonaws.com" }
      Action    = "sts:AssumeRole"
    }]
  })
}

resource "aws_iam_role_policy" "agentcore_ecr" {
  role = aws_iam_role.agentcore.id
  policy = jsonencode({
    Version = "2012-10-17"
    Statement = [{
      Effect = "Allow"
      Action = [
        "ecr:GetAuthorizationToken",
        "ecr:BatchGetImage",
        "ecr:GetDownloadUrlForLayer"
      ]
      Resource = "*"
    }]
  })
}
```

---

## Memory の作成

```hcl
resource "aws_bedrockagentcore_memory" "example" {
  name                  = "my-memory"
  event_expiry_duration = 30  # 日数（7〜365）
}
```

---

## Lambda から AgentCore を呼び出す

### Python（boto3）

```python
import boto3

# エージェント実行（ストリーミング）
client = boto3.client('bedrock-agentcore')
response = client.invoke_agent_runtime(
    agentRuntimeId='xxxxxxxx',
    # セッション管理・入力等のパラメータ
)

# メモリ操作
client.batch_create_memory_records(...)
client.retrieve_memory_records(...)

# 管理操作（作成・削除・一覧）は別クライアント
ctrl = boto3.client('bedrock-agentcore-control')
ctrl.create_agent_runtime(...)
```

### Node.js（Hono Lambda から呼び出す場合）

AWS SDK v3 の `@aws-sdk/client-bedrock-agentcore` に相当するパッケージを使う。
パッケージ名・API 形式は公式ドキュメントで要確認（2026年3月時点で情報が少ない）。

### Lambda 実行ロールに必要な IAM 権限

**注意: 正確な IAM アクション名は未確認。** 以下は推測を含む。

```json
{
  "Effect": "Allow",
  "Action": [
    "bedrock-agentcore:InvokeAgentRuntime"
  ],
  "Resource": "*"
}
```

→ デプロイ時に実際のエラーメッセージで正確なアクション名を確認すること。

---

## Gateway（Lambda を MCP ツールとして公開）

Lambda を AgentCore の MCP ゲートウェイ経由で呼び出す構成。

```hcl
resource "aws_bedrockagentcore_gateway" "example" {
  name            = "my-gateway"
  role_arn        = aws_iam_role.gateway.arn
  protocol_type   = "MCP"         # 現時点では MCP のみ
  authorizer_type = "CUSTOM_JWT"  # または "AWS_IAM"

  authorizer_configuration {
    custom_jwt_authorizer {
      discovery_url    = "https://example.com/.well-known/openid-configuration"
      allowed_audience = ["my-audience"]
    }
  }
}

resource "aws_bedrockagentcore_gateway_target" "example" {
  gateway_identifier = aws_bedrockagentcore_gateway.example.id
  name               = "my-lambda-tool"

  target_configuration {
    lambda {
      lambda_arn = aws_lambda_function.my_tool.arn
    }
  }
}
```

---

## このプロジェクトでの利用構成（検討中）

```
Client
  → CloudFront (*.cloudfront.net)
      ├── /api/invoke*  → API-GW (HTTP API, Streaming)
      │                     → Lambda (Hono, Node.js 22.x)
      │                         → AgentCore Runtime（エージェント本体）
      ├── /api/history* → API-GW (HTTP API, Buffered)
      │                     → Lambda (Hono)
      │                         → DynamoDB（会話履歴）
      └── /*            → S3（シンプルなチャット UI）
```

### Hono Lambda の役割
- `/api/invoke`: API キー検証 → AgentCore を呼び出してストリーミングレスポンスをそのまま返す
- `/api/history`: API キー検証 → DynamoDB から履歴を取得・保存・削除

### AgentCore Runtime の役割
- エージェントロジック本体（モデル呼び出し・ツール実行）
- デプロイ方式: コンテナ（ECR）または コード（S3）← 未決定

### 決定事項
- **用途**: 一般的な LLM チャット（Claude モデルとの会話）
- **AgentCore コード**: 最小構成 Python — ユーザーメッセージを受け取り Bedrock Claude を呼び出して返すだけ
- **デプロイ方式**: コード（S3）方式（コンテナ不要で簡易）
- **フロントエンド**: シンプルな HTML/JS チャット UI（メッセージ送信・ストリーミング表示・履歴閲覧）

### 未決事項（次回セッションで確認）
- Lambda → AgentCore の正確な SDK 呼び出し方法（Node.js）
- Lambda 実行ロールの正確な IAM アクション名

---

## 参考

- Terraform AWS Provider リリースノート: https://github.com/hashicorp/terraform-provider-aws/blob/main/CHANGELOG.md
- AgentCore リソースの Terraform ドキュメント: https://github.com/hashicorp/terraform-provider-aws/tree/main/website/docs/r/（`bedrockagentcore` で検索）
