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

variable "api_key" {
  description = "管理WebページのAPIキー（x-api-keyヘッダーで使用）"
  type        = string
  sensitive   = true
}
