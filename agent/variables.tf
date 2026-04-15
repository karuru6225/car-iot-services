variable "aws_region" {
  description = "AWSリージョン"
  default     = "ap-northeast-1"
}

variable "project" {
  description = "プロジェクト名（リソース名のプレフィックス）"
  default     = "iot-monitor-agent"
}

variable "api_key" {
  description = "Hono ミドルウェアで検証する API キー（Authorization ヘッダーに Bearer トークンとして渡す）"
  type        = string
  sensitive   = true
}

variable "bedrock_model_id" {
  description = "使用する Bedrock モデル ID"
  default     = "anthropic.claude-3-5-sonnet-20241022-v2:0"
}
