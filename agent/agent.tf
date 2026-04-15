locals {
  lambda_src_dir    = "${path.module}/lambda_src"
  agentcore_src_dir = "${path.module}/agentcore_src"
  build_dir         = "${path.module}/.build"
  # AgentCore はハイフン不可（^[a-zA-Z][a-zA-Z0-9_]{0,47}$）のでアンダースコアに変換
  agentcore_name = replace(var.project, "-", "_")
}

# ─── AgentCore: コードを置く S3 バケット ──────────────────────────────────────

resource "aws_s3_bucket" "agentcore_code" {
  bucket = "${var.project}-agentcore-code-${data.aws_caller_identity.current.account_id}"
}

resource "aws_s3_bucket_public_access_block" "agentcore_code" {
  bucket                  = aws_s3_bucket.agentcore_code.id
  block_public_acls       = true
  block_public_policy     = true
  ignore_public_acls      = true
  restrict_public_buckets = true
}

data "archive_file" "agentcore_code" {
  type        = "zip"
  source_dir  = local.agentcore_src_dir
  output_path = "${local.build_dir}/agentcore.zip"
}

resource "aws_s3_object" "agentcore_code" {
  bucket = aws_s3_bucket.agentcore_code.id
  key    = "agentcore.zip"
  source = data.archive_file.agentcore_code.output_path
  etag   = data.archive_file.agentcore_code.output_md5
}

# ─── AgentCore: IAM ロール ────────────────────────────────────────────────────

resource "aws_iam_role" "agentcore" {
  name = "${var.project}-agentcore"
  assume_role_policy = jsonencode({
    Version = "2012-10-17"
    Statement = [{
      Effect    = "Allow"
      Principal = { Service = "bedrock-agentcore.amazonaws.com" }
      Action    = "sts:AssumeRole"
    }]
  })
}

resource "aws_iam_role_policy" "agentcore_s3" {
  name = "${var.project}-agentcore-s3"
  role = aws_iam_role.agentcore.id
  policy = jsonencode({
    Version = "2012-10-17"
    Statement = [{
      Effect   = "Allow"
      Action   = ["s3:GetObject"]
      Resource = "${aws_s3_bucket.agentcore_code.arn}/*"
    }]
  })
}

resource "aws_iam_role_policy" "agentcore_bedrock" {
  name = "${var.project}-agentcore-bedrock"
  role = aws_iam_role.agentcore.id
  policy = jsonencode({
    Version = "2012-10-17"
    Statement = [{
      Effect   = "Allow"
      Action   = ["bedrock:InvokeModel", "bedrock:InvokeModelWithResponseStream"]
      Resource = "arn:aws:bedrock:${var.aws_region}::foundation-model/${var.bedrock_model_id}"
    }]
  })
}

# ─── AgentCore ランタイム ─────────────────────────────────────────────────────

resource "aws_bedrockagentcore_agent_runtime" "main" {
  agent_runtime_name = local.agentcore_name
  role_arn           = aws_iam_role.agentcore.arn

  agent_runtime_artifact {
    code_configuration {
      entry_point = ["index.py"]
      runtime     = "PYTHON_3_13"
      code {
        s3 {
          bucket = aws_s3_bucket.agentcore_code.bucket
          prefix = aws_s3_object.agentcore_code.key
        }
      }
    }
  }

  network_configuration {
    network_mode = "PUBLIC"
  }

  environment_variables = {
    MODEL_ID = var.bedrock_model_id
  }

  depends_on = [aws_s3_object.agentcore_code]
}

# ─── DynamoDB: 会話履歴テーブル ───────────────────────────────────────────────

resource "aws_dynamodb_table" "history" {
  name         = "${var.project}-history"
  billing_mode = "PAY_PER_REQUEST"
  hash_key     = "userId"
  range_key    = "timestamp"

  attribute {
    name = "userId"
    type = "S"
  }

  attribute {
    name = "timestamp"
    type = "S"
  }

  ttl {
    attribute_name = "ttl"
    enabled        = true
  }
}

# ─── Lambda (Python): zip ────────────────────────────────────────────────────

data "archive_file" "lambda" {
  type        = "zip"
  source_dir  = local.lambda_src_dir
  output_path = "${local.build_dir}/lambda.zip"
}

# ─── Lambda (Python): IAM ロール ─────────────────────────────────────────────

resource "aws_iam_role" "lambda" {
  name = "${var.project}-lambda"
  assume_role_policy = jsonencode({
    Version = "2012-10-17"
    Statement = [{
      Effect    = "Allow"
      Principal = { Service = "lambda.amazonaws.com" }
      Action    = "sts:AssumeRole"
    }]
  })
}

resource "aws_iam_role_policy_attachment" "lambda_basic" {
  role       = aws_iam_role.lambda.name
  policy_arn = "arn:aws:iam::aws:policy/service-role/AWSLambdaBasicExecutionRole"
}

resource "aws_iam_role_policy" "lambda_dynamodb" {
  name = "${var.project}-lambda-dynamodb"
  role = aws_iam_role.lambda.id
  policy = jsonencode({
    Version = "2012-10-17"
    Statement = [{
      Effect   = "Allow"
      Action   = ["dynamodb:PutItem", "dynamodb:GetItem", "dynamodb:Query", "dynamodb:DeleteItem"]
      Resource = aws_dynamodb_table.history.arn
    }]
  })
}

resource "aws_iam_role_policy" "lambda_agentcore" {
  name = "${var.project}-lambda-agentcore"
  role = aws_iam_role.lambda.id
  policy = jsonencode({
    Version = "2012-10-17"
    Statement = [{
      Effect   = "Allow"
      # ノート: 正確な IAM アクション名は要確認。デプロイ時のエラーで修正する
      Action   = ["bedrock-agentcore:InvokeAgentRuntime"]
      Resource = aws_bedrockagentcore_agent_runtime.main.agent_runtime_arn
    }]
  })
}

# ─── Lambda (Python): 関数 ───────────────────────────────────────────────────

resource "aws_lambda_function" "main" {
  function_name    = "${var.project}-api"
  filename         = data.archive_file.lambda.output_path
  source_code_hash = data.archive_file.lambda.output_base64sha256
  runtime          = "python3.13"
  handler          = "index.handler"
  role             = aws_iam_role.lambda.arn
  timeout          = 60

  environment {
    variables = {
      API_KEY              = var.api_key
      HISTORY_TABLE        = aws_dynamodb_table.history.name
      AGENTCORE_RUNTIME_ARN = aws_bedrockagentcore_agent_runtime.main.agent_runtime_arn
    }
  }
}

# ─── API Gateway (Streaming) ──────────────────────────────────────────────────

resource "aws_apigatewayv2_api" "stream" {
  name          = "${var.project}-stream"
  protocol_type = "HTTP"

  cors_configuration {
    allow_origins = ["*"]
    allow_methods = ["POST", "OPTIONS"]
    allow_headers = ["Content-Type", "Authorization"]
  }
}

resource "aws_apigatewayv2_stage" "stream" {
  api_id      = aws_apigatewayv2_api.stream.id
  name        = "$default"
  auto_deploy = true

  default_route_settings {
    throttling_burst_limit = 5
    throttling_rate_limit  = 2
  }
}

resource "aws_apigatewayv2_integration" "stream" {
  api_id                 = aws_apigatewayv2_api.stream.id
  integration_type       = "AWS_PROXY"
  integration_uri        = aws_lambda_function.main.invoke_arn
  payload_format_version = "2.0"
}

resource "aws_apigatewayv2_route" "invoke" {
  api_id    = aws_apigatewayv2_api.stream.id
  route_key = "POST /{proxy+}"
  target    = "integrations/${aws_apigatewayv2_integration.stream.id}"
}

resource "aws_lambda_permission" "stream" {
  statement_id  = "AllowStreamAPIGW"
  action        = "lambda:InvokeFunction"
  function_name = aws_lambda_function.main.function_name
  principal     = "apigateway.amazonaws.com"
  source_arn    = "${aws_apigatewayv2_api.stream.execution_arn}/*/*"
}

# ─── API Gateway (Buffered) ───────────────────────────────────────────────────

resource "aws_apigatewayv2_api" "buffered" {
  name          = "${var.project}-buffered"
  protocol_type = "HTTP"

  cors_configuration {
    allow_origins = ["*"]
    allow_methods = ["GET", "DELETE", "OPTIONS"]
    allow_headers = ["Content-Type", "Authorization"]
  }
}

resource "aws_apigatewayv2_stage" "buffered" {
  api_id      = aws_apigatewayv2_api.buffered.id
  name        = "$default"
  auto_deploy = true

  default_route_settings {
    throttling_burst_limit = 10
    throttling_rate_limit  = 5
  }
}

resource "aws_apigatewayv2_integration" "buffered" {
  api_id                 = aws_apigatewayv2_api.buffered.id
  integration_type       = "AWS_PROXY"
  integration_uri        = aws_lambda_function.main.invoke_arn
  payload_format_version = "2.0"
}

resource "aws_apigatewayv2_route" "history" {
  api_id    = aws_apigatewayv2_api.buffered.id
  route_key = "ANY /{proxy+}"
  target    = "integrations/${aws_apigatewayv2_integration.buffered.id}"
}

resource "aws_lambda_permission" "buffered" {
  statement_id  = "AllowBufferedAPIGW"
  action        = "lambda:InvokeFunction"
  function_name = aws_lambda_function.main.function_name
  principal     = "apigateway.amazonaws.com"
  source_arn    = "${aws_apigatewayv2_api.buffered.execution_arn}/*/*"
}

# ─── S3: チャット UI ──────────────────────────────────────────────────────────

resource "aws_s3_bucket" "web" {
  bucket = "${var.project}-web-${data.aws_caller_identity.current.account_id}"
}

resource "aws_s3_bucket_public_access_block" "web" {
  bucket                  = aws_s3_bucket.web.id
  block_public_acls       = true
  block_public_policy     = true
  ignore_public_acls      = true
  restrict_public_buckets = true
}

resource "aws_cloudfront_origin_access_control" "web" {
  name                              = "${var.project}-web"
  origin_access_control_origin_type = "s3"
  signing_behavior                  = "always"
  signing_protocol                  = "sigv4"
}

# ─── CloudFront ───────────────────────────────────────────────────────────────

locals {
  stream_api_domain   = replace(aws_apigatewayv2_stage.stream.invoke_url, "/^https?://([^/]+).*/", "$1")
  buffered_api_domain = replace(aws_apigatewayv2_stage.buffered.invoke_url, "/^https?://([^/]+).*/", "$1")
}

resource "aws_cloudfront_distribution" "main" {
  enabled             = true
  default_root_object = "index.html"
  price_class         = "PriceClass_100"

  # オリジン1: Streaming API-GW
  origin {
    domain_name = local.stream_api_domain
    origin_id   = "apigw-stream"

    custom_origin_config {
      http_port              = 80
      https_port             = 443
      origin_protocol_policy = "https-only"
      origin_ssl_protocols   = ["TLSv1.2"]
    }
  }

  # オリジン2: Buffered API-GW
  origin {
    domain_name = local.buffered_api_domain
    origin_id   = "apigw-buffered"

    custom_origin_config {
      http_port              = 80
      https_port             = 443
      origin_protocol_policy = "https-only"
      origin_ssl_protocols   = ["TLSv1.2"]
    }
  }

  # オリジン3: S3 Web
  origin {
    domain_name              = aws_s3_bucket.web.bucket_regional_domain_name
    origin_id                = "s3-web"
    origin_access_control_id = aws_cloudfront_origin_access_control.web.id
  }

  # /api/invoke* → Streaming API-GW
  ordered_cache_behavior {
    path_pattern           = "/api/invoke*"
    target_origin_id       = "apigw-stream"
    viewer_protocol_policy = "redirect-to-https"
    allowed_methods        = ["GET", "HEAD", "OPTIONS", "PUT", "POST", "PATCH", "DELETE"]
    cached_methods         = ["GET", "HEAD"]
    compress               = false

    forwarded_values {
      query_string = true
      headers      = ["Authorization", "Content-Type"]
      cookies { forward = "none" }
    }

    min_ttl     = 0
    default_ttl = 0
    max_ttl     = 0
  }

  # /api/history* → Buffered API-GW
  ordered_cache_behavior {
    path_pattern           = "/api/history*"
    target_origin_id       = "apigw-buffered"
    viewer_protocol_policy = "redirect-to-https"
    allowed_methods        = ["GET", "HEAD", "OPTIONS", "PUT", "POST", "PATCH", "DELETE"]
    cached_methods         = ["GET", "HEAD"]
    compress               = false

    forwarded_values {
      query_string = true
      headers      = ["Authorization", "Content-Type"]
      cookies { forward = "none" }
    }

    min_ttl     = 0
    default_ttl = 0
    max_ttl     = 0
  }

  # /* → S3（デフォルト）
  default_cache_behavior {
    target_origin_id       = "s3-web"
    viewer_protocol_policy = "redirect-to-https"
    allowed_methods        = ["GET", "HEAD"]
    cached_methods         = ["GET", "HEAD"]
    compress               = true

    forwarded_values {
      query_string = false
      cookies { forward = "none" }
    }

    min_ttl     = 0
    default_ttl = 300
    max_ttl     = 3600
  }

  restrictions {
    geo_restriction { restriction_type = "none" }
  }

  viewer_certificate {
    cloudfront_default_certificate = true
  }
}

resource "aws_s3_bucket_policy" "web" {
  bucket = aws_s3_bucket.web.id
  policy = jsonencode({
    Version = "2012-10-17"
    Statement = [{
      Sid    = "AllowCloudFront"
      Effect = "Allow"
      Principal = {
        Service = "cloudfront.amazonaws.com"
      }
      Action   = "s3:GetObject"
      Resource = "${aws_s3_bucket.web.arn}/*"
      Condition = {
        StringEquals = {
          "AWS:SourceArn" = aws_cloudfront_distribution.main.arn
        }
      }
    }]
  })
}

# ─── チャット UI を S3 にアップロード ─────────────────────────────────────────

resource "aws_s3_object" "index_html" {
  bucket       = aws_s3_bucket.web.id
  key          = "index.html"
  source       = "${path.module}/web/index.html"
  content_type = "text/html"
  etag         = filemd5("${path.module}/web/index.html")
}
