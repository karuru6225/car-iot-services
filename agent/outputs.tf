output "cloudfront_domain" {
  description = "チャット UI の URL"
  value       = "https://${aws_cloudfront_distribution.main.domain_name}"
}

output "stream_api_url" {
  description = "Streaming API-GW の URL（直接アクセス用）"
  value       = aws_apigatewayv2_stage.stream.invoke_url
}

output "buffered_api_url" {
  description = "Buffered API-GW の URL（直接アクセス用）"
  value       = aws_apigatewayv2_stage.buffered.invoke_url
}

output "agentcore_runtime_id" {
  description = "AgentCore ランタイム ID"
  value       = aws_bedrockagentcore_agent_runtime.main.agent_runtime_id
}
