locals {
  ingest_src_dir = "${path.module}/lambda_src/ingest"
  query_src_dir  = "${path.module}/lambda_src/query"
  delete_src_dir = "${path.module}/lambda_src/delete"
  build_dir      = "${path.module}/.build"
}

data "archive_file" "ingest" {
  type        = "zip"
  source_dir  = local.ingest_src_dir
  output_path = "${local.build_dir}/ingest.zip"
}

data "archive_file" "query" {
  type        = "zip"
  source_dir  = local.query_src_dir
  output_path = "${local.build_dir}/query.zip"
}

data "archive_file" "delete" {
  type        = "zip"
  source_dir  = local.delete_src_dir
  output_path = "${local.build_dir}/delete.zip"
}

# ─── ingest Lambda（IoT Core → S3 書き込み） ─────────────────────────────────

resource "aws_lambda_function" "ingest" {
  function_name    = "${var.project}-ingest"
  filename         = data.archive_file.ingest.output_path
  source_code_hash = data.archive_file.ingest.output_base64sha256
  runtime          = "python3.12"
  handler          = "index.handler"
  role             = aws_iam_role.lambda_ingest.arn
  timeout          = 10

  environment {
    variables = {
      S3_BUCKET = aws_s3_bucket.main.bucket
    }
  }
}

# ─── query Lambda（API GW → Athena クエリ） ───────────────────────────────────

resource "aws_lambda_function" "query" {
  function_name    = "${var.project}-query"
  filename         = data.archive_file.query.output_path
  source_code_hash = data.archive_file.query.output_base64sha256
  runtime          = "python3.12"
  handler          = "index.handler"
  role             = aws_iam_role.lambda_query.arn
  timeout          = 30

  environment {
    variables = {
      S3_BUCKET        = aws_s3_bucket.main.bucket
      ATHENA_DATABASE  = local.glue_db_name
      ATHENA_WORKGROUP = aws_athena_workgroup.main.name
    }
  }
}

# ─── delete Lambda（API GW → S3 削除） ────────────────────────────────────────

resource "aws_lambda_function" "delete" {
  function_name    = "${var.project}-delete"
  filename         = data.archive_file.delete.output_path
  source_code_hash = data.archive_file.delete.output_base64sha256
  runtime          = "python3.12"
  handler          = "index.handler"
  role             = aws_iam_role.lambda_delete.arn
  timeout          = 60

  environment {
    variables = {
      S3_BUCKET        = aws_s3_bucket.main.bucket
      ATHENA_DATABASE  = local.glue_db_name
      ATHENA_WORKGROUP = aws_athena_workgroup.main.name
    }
  }
}

# ─── API Gateway HTTP API ─────────────────────────────────────────────────────

resource "aws_apigatewayv2_api" "main" {
  name          = var.project
  protocol_type = "HTTP"

  cors_configuration {
    allow_origins = ["https://${local.web_domain}"]
    allow_methods = ["GET", "DELETE", "OPTIONS"]
    allow_headers = ["Content-Type", "Authorization"]
  }
}

resource "aws_apigatewayv2_stage" "main" {
  api_id      = aws_apigatewayv2_api.main.id
  name        = "$default"
  auto_deploy = true

  default_route_settings {
    throttling_burst_limit = 10
    throttling_rate_limit  = 5
  }
}

# ─── JWT Authorizer（Cognito） ────────────────────────────────────────────────

resource "aws_apigatewayv2_authorizer" "cognito" {
  api_id           = aws_apigatewayv2_api.main.id
  authorizer_type  = "JWT"
  name             = "${var.project}-cognito"
  identity_sources = ["$request.header.Authorization"]

  jwt_configuration {
    audience = [aws_cognito_user_pool_client.web.id]
    issuer   = "https://cognito-idp.${var.aws_region}.amazonaws.com/${aws_cognito_user_pool.main.id}"
  }
}

# ─── Integrations ─────────────────────────────────────────────────────────────

resource "aws_apigatewayv2_integration" "query" {
  api_id                 = aws_apigatewayv2_api.main.id
  integration_type       = "AWS_PROXY"
  integration_uri        = aws_lambda_function.query.invoke_arn
  payload_format_version = "2.0"
}

resource "aws_apigatewayv2_integration" "delete" {
  api_id                 = aws_apigatewayv2_api.main.id
  integration_type       = "AWS_PROXY"
  integration_uri        = aws_lambda_function.delete.invoke_arn
  payload_format_version = "2.0"
}

# ─── Routes ───────────────────────────────────────────────────────────────────

resource "aws_apigatewayv2_route" "query" {
  api_id             = aws_apigatewayv2_api.main.id
  route_key          = "GET /data"
  target             = "integrations/${aws_apigatewayv2_integration.query.id}"
  authorizer_id      = aws_apigatewayv2_authorizer.cognito.id
  authorization_type = "JWT"
}

resource "aws_apigatewayv2_route" "delete" {
  api_id             = aws_apigatewayv2_api.main.id
  route_key          = "DELETE /data"
  target             = "integrations/${aws_apigatewayv2_integration.delete.id}"
  authorizer_id      = aws_apigatewayv2_authorizer.cognito.id
  authorization_type = "JWT"
}

# ─── Lambda Permissions ───────────────────────────────────────────────────────

resource "aws_lambda_permission" "apigw_query" {
  statement_id  = "AllowAPIGWInvoke"
  action        = "lambda:InvokeFunction"
  function_name = aws_lambda_function.query.function_name
  principal     = "apigateway.amazonaws.com"
  source_arn    = "${aws_apigatewayv2_api.main.execution_arn}/*/*"
}

resource "aws_lambda_permission" "apigw_delete" {
  statement_id  = "AllowAPIGWInvoke"
  action        = "lambda:InvokeFunction"
  function_name = aws_lambda_function.delete.function_name
  principal     = "apigateway.amazonaws.com"
  source_arn    = "${aws_apigatewayv2_api.main.execution_arn}/*/*"
}
