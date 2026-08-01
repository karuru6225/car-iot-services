variable "aws_region" {
  description = "AWSリージョン"
  default     = "ap-northeast-1"
}

variable "project" {
  description = "プロジェクト名（リソース名のプレフィックス）"
  default     = "iot-monitor"
}

variable "device_id" {
  description = "IoT Thing名（デバイスID）"
  default     = "iot-monitor-gw-001"
}

variable "hosted_zone_id" {
  description = "Route53 ホストゾーン ID"
  type        = string
}

variable "web_subdomain" {
  description = "Web管理画面のサブドメイン（例: iot → iot.example.com）"
  type        = string
}

variable "google_oauth_client_id" {
  description = "Google Cloud ConsoleのOAuthクライアントID（Cognito Google IdP連携用）"
  type        = string
  sensitive   = true
}

variable "google_oauth_client_secret" {
  description = "Google Cloud ConsoleのOAuthクライアントシークレット（Cognito Google IdP連携用）"
  type        = string
  sensitive   = true
}
