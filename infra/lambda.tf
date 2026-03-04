locals {
  ingest_src_dir     = "${path.module}/lambda_src/ingest"
  query_src_dir      = "${path.module}/lambda_src/query"
  delete_src_dir     = "${path.module}/lambda_src/delete"
  authorizer_src_dir = "${path.module}/lambda_src/authorizer"
  build_dir          = "${path.module}/.build"
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

data "archive_file" "authorizer" {
  type        = "zip"
  source_dir  = local.authorizer_src_dir
  output_path = "${local.build_dir}/authorizer.zip"
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

# ─── authorizer Lambda（API キー検証） ────────────────────────────────────────

resource "aws_lambda_function" "authorizer" {
  function_name    = "${var.project}-authorizer"
  filename         = data.archive_file.authorizer.output_path
  source_code_hash = data.archive_file.authorizer.output_base64sha256
  runtime          = "python3.12"
  handler          = "index.handler"
  role             = aws_iam_role.lambda_authorizer.arn
  timeout          = 5

  environment {
    variables = {
      API_KEY = var.api_key
    }
  }
}

# ─── API Gateway HTTP API ─────────────────────────────────────────────────────

resource "aws_apigatewayv2_api" "main" {
  name          = var.project
  protocol_type = "HTTP"

  cors_configuration {
    allow_origins = ["*"]
    allow_methods = ["GET", "DELETE", "OPTIONS"]
    allow_headers = ["Content-Type", "x-api-key"]
  }
}

resource "aws_apigatewayv2_stage" "main" {
  api_id      = aws_apigatewayv2_api.main.id
  name        = "$default"
  auto_deploy = true
}

# ─── Lambda Authorizer ────────────────────────────────────────────────────────

resource "aws_apigatewayv2_authorizer" "api_key" {
  api_id                            = aws_apigatewayv2_api.main.id
  authorizer_type                   = "REQUEST"
  name                              = "${var.project}-api-key"
  authorizer_uri                    = aws_lambda_function.authorizer.invoke_arn
  identity_sources                  = ["$request.header.x-api-key"]
  authorizer_payload_format_version = "2.0"
  enable_simple_responses           = true
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
  authorizer_id      = aws_apigatewayv2_authorizer.api_key.id
  authorization_type = "CUSTOM"
}

resource "aws_apigatewayv2_route" "delete" {
  api_id             = aws_apigatewayv2_api.main.id
  route_key          = "DELETE /data"
  target             = "integrations/${aws_apigatewayv2_integration.delete.id}"
  authorizer_id      = aws_apigatewayv2_authorizer.api_key.id
  authorization_type = "CUSTOM"
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

resource "aws_lambda_permission" "apigw_authorizer" {
  statement_id  = "AllowAPIGWInvoke"
  action        = "lambda:InvokeFunction"
  function_name = aws_lambda_function.authorizer.function_name
  principal     = "apigateway.amazonaws.com"
  source_arn    = "${aws_apigatewayv2_api.main.execution_arn}/*/*"
}
