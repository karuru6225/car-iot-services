output "iot_endpoint" {
  description = "IoT Core の MQTT エンドポイント（デバイスに設定する）"
  value       = data.aws_iot_endpoint.main.endpoint_address
}

output "api_endpoint" {
  description = "データ取得 API のエンドポイント"
  value       = aws_apigatewayv2_stage.main.invoke_url
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
  description = "管理Web ページの URL"
  value       = "https://${local.web_domain}"
}

output "web_bucket" {
  description = "Web 静的ファイル用 S3 バケット名"
  value       = aws_s3_bucket.web.bucket
}

output "cognito_user_pool_id" {
  description = "Cognito User Pool ID（ユーザー追加時に使用）"
  value       = aws_cognito_user_pool.main.id
}

output "cognito_client_id" {
  description = "Cognito App Client ID"
  value       = aws_cognito_user_pool_client.web.id
}

output "cognito_login_url" {
  description = "Cognito Hosted UI のログイン URL"
  value       = "${local.cognito_domain_base}/login?client_id=${aws_cognito_user_pool_client.web.id}&response_type=token&scope=openid+email+profile&redirect_uri=https://${local.web_domain}"
}
