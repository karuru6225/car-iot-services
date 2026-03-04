output "iot_endpoint" {
  description = "IoT Core の MQTT エンドポイント（デバイスに設定する）"
  value       = data.aws_iot_endpoint.main.endpoint_address
}

output "api_endpoint" {
  description = "Flutter 向けデータ取得 API のエンドポイント"
  value       = "${aws_apigatewayv2_stage.main.invoke_url}"
}

output "s3_bucket" {
  description = "センサーデータ保存バケット名"
  value       = aws_s3_bucket.main.bucket
}

output "secrets_manager_arn" {
  description = "デバイス証明書の Secrets Manager ARN（ここから証明書を取得してデバイスに書き込む）"
  value       = aws_secretsmanager_secret.device_cert.arn
}

output "web_url" {
  description = "管理Web ページの URL（CloudFront）"
  value       = "https://${aws_cloudfront_distribution.web.domain_name}"
}

output "web_bucket" {
  description = "Web 静的ファイル用 S3 バケット名（index.html をここにアップロードする）"
  value       = aws_s3_bucket.web.bucket
}
