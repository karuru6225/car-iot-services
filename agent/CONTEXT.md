# agent/ 引き継ぎ資料

作成日: 2026-03-19

## 関連ドキュメント（まず読むこと）

- `../AgentUI_Plan.md` — 全体計画・ディレクトリ構成・デプロイ手順・既知の注意点
- `../AgentCore.md` — Amazon Bedrock AgentCore の概要・Terraform サポート状況・SDK 呼び出し方法

---

## このディレクトリの目的

Lambdalith + Streaming/Buffered API-GW 分割 + CloudFront ルーティングの構成を試すための試験実装。
参考: [スクショの構成](../AgentUI_Plan.md#アーキテクチャ)

```
Client → CloudFront
  ├── /api/invoke*  → API-GW (Streaming) → Lambda (Python) → AgentCore → Bedrock Claude
  ├── /api/history* → API-GW (Buffered)  → Lambda (Python) → DynamoDB
  └── /*            → S3（チャット UI）
```

---

## 現在の状態（2026-03-19 時点）

### 完了済み
- Terraform 全ファイル（`agent.tf`, `main.tf`, `variables.tf`, `outputs.tf`）
- Lambda Python コード（`lambda_src/index.py`）
- AgentCore Python コード（`agentcore_src/index.py`）
- チャット UI（`web/index.html`）
- `terraform apply` は通過済み

### 作業中・未解決
- **AgentCore のレスポンス形式が不明**
  - `invoke_agent_runtime` の引数は CloudWatch エラーで判明・修正済み：
    - `agentRuntimeArn`（ARN）、`runtimeSessionId`、`payload`（bytes）
  - レスポンスの `body` キーの実際の型・構造がまだ未確認
  - 最後の試行では Lambda が internal server error で止まっており、次回ログを確認して修正する

---

## デプロイ手順

```powershell
# terraform.tfvars がなければ作成
cp terraform.tfvars.example terraform.tfvars
# terraform.tfvars の api_key を設定

.\manage.ps1 apply
```

Lambda コードを変更した場合も `.\manage.ps1 apply` で反映される（ビルド不要）。

---

## ファイル構成

```
agent/
├── main.tf                   # provider + S3 backend
├── variables.tf              # aws_region, project, api_key, bedrock_model_id
├── outputs.tf                # CloudFront URL, API-GW URL, AgentCore runtime ID
├── agent.tf                  # 全リソース定義
├── terraform.tfvars          # 実値（gitignore 対象）
├── terraform.tfvars.example
├── backend.tfbackend         # key = "agent/terraform.tfstate"
├── backend.tfbackend.example
├── manage.ps1
├── lambda_src/
│   ├── index.py              # Lambda 本体（ルーティング・AgentCore呼び出し・DynamoDB）
│   └── hono/                 # 参考用（使用しない）
├── agentcore_src/
│   └── index.py              # AgentCore ランタイムに載せる Python コード
└── web/
    └── index.html            # チャット UI
```

---

## 次にやること

1. Lambda の CloudWatch ログで `invoke_agent_runtime` レスポンスの構造を確認
2. `lambda_src/index.py` の `handle_invoke` でレスポンスのパースを修正
3. チャット UI からエンドツーエンドで動作確認
