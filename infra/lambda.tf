locals {
  ingest_src_dir        = "${path.module}/lambda_src/ingest"
  query_src_dir         = "${path.module}/lambda_src/query"
  delete_src_dir        = "${path.module}/lambda_src/delete"
  labels_src_dir        = "${path.module}/lambda_src/labels"
  status_src_dir        = "${path.module}/lambda_src/status"
  admin_src_dir         = "${path.module}/lambda_src/admin"
  shadow_guard_src_dir  = "${path.module}/lambda_src/shadow_guard"
  shadow_events_src_dir = "${path.module}/lambda_src/shadow_events"
  compact_src_dir       = "${path.module}/lambda_src/compact"
  build_dir             = "${path.module}/.build"
}

data "archive_file" "ingest" {
  type        = "zip"
  source_dir  = local.ingest_src_dir
  output_path = "${local.build_dir}/ingest.zip"
  excludes    = ["tests"]
}

data "archive_file" "query" {
  type        = "zip"
  source_dir  = local.query_src_dir
  output_path = "${local.build_dir}/query.zip"
  excludes    = ["tests"]
}

data "archive_file" "delete" {
  type        = "zip"
  source_dir  = local.delete_src_dir
  output_path = "${local.build_dir}/delete.zip"
  excludes    = ["tests"]
}

data "archive_file" "labels" {
  type        = "zip"
  source_dir  = local.labels_src_dir
  output_path = "${local.build_dir}/labels.zip"
  excludes    = ["tests"]
}

data "archive_file" "status" {
  type        = "zip"
  source_dir  = local.status_src_dir
  output_path = "${local.build_dir}/status.zip"
  excludes    = ["tests"]
}

data "archive_file" "admin" {
  type        = "zip"
  source_dir  = local.admin_src_dir
  output_path = "${local.build_dir}/admin.zip"
  excludes    = ["tests"]
}

data "archive_file" "shadow_guard" {
  type        = "zip"
  source_dir  = local.shadow_guard_src_dir
  output_path = "${local.build_dir}/shadow_guard.zip"
  excludes    = ["tests"]
}

data "archive_file" "shadow_events" {
  type        = "zip"
  source_dir  = local.shadow_events_src_dir
  output_path = "${local.build_dir}/shadow_events.zip"
  excludes    = ["tests"]
}

data "archive_file" "compact" {
  type        = "zip"
  source_dir  = local.compact_src_dir
  output_path = "${local.build_dir}/compact.zip"
  excludes    = ["tests"]
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
      S3_BUCKET        = aws_s3_bucket.main.bucket
      CORRUPTED_BUCKET = aws_s3_bucket.corrupted.bucket
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

# ─── labels Lambda（GET/PUT /labels） ────────────────────────────────────────

resource "aws_lambda_function" "labels" {
  function_name    = "${var.project}-labels"
  filename         = data.archive_file.labels.output_path
  source_code_hash = data.archive_file.labels.output_base64sha256
  runtime          = "python3.12"
  handler          = "index.handler"
  role             = aws_iam_role.lambda_labels.arn
  timeout          = 10

  environment {
    variables = {
      S3_BUCKET = aws_s3_bucket.main.bucket
    }
  }
}

# ─── status Lambda（API GW → IoT Shadow 読み取り） ───────────────────────────

resource "aws_lambda_function" "status" {
  function_name    = "${var.project}-status"
  filename         = data.archive_file.status.output_path
  source_code_hash = data.archive_file.status.output_base64sha256
  runtime          = "python3.12"
  handler          = "index.handler"
  role             = aws_iam_role.lambda_status.arn
  timeout          = 10

  environment {
    variables = {
      THING_NAME   = var.device_id
      IOT_ENDPOINT = "https://${data.aws_iot_endpoint.main.endpoint_address}"
    }
  }
}

# ─── shadow_guard Lambda（Shadow update/accepted → 不正キー検知・修正） ─────

resource "aws_lambda_function" "shadow_guard" {
  function_name    = "${var.project}-shadow-guard"
  filename         = data.archive_file.shadow_guard.output_path
  source_code_hash = data.archive_file.shadow_guard.output_base64sha256
  runtime          = "python3.12"
  handler          = "index.handler"
  role             = aws_iam_role.lambda_shadow_guard.arn
  timeout          = 10

  environment {
    variables = {
      IOT_ENDPOINT = "https://${data.aws_iot_endpoint.main.endpoint_address}"
    }
  }
}

# ─── shadow_events Lambda（Shadow update/documents → reported変化イベントをDynamoDBに記録） ───

resource "aws_lambda_function" "shadow_events" {
  function_name    = "${var.project}-shadow-events"
  filename         = data.archive_file.shadow_events.output_path
  source_code_hash = data.archive_file.shadow_events.output_base64sha256
  runtime          = "python3.12"
  handler          = "index.handler"
  role             = aws_iam_role.lambda_shadow_events.arn
  timeout          = 10

  environment {
    variables = {
      TABLE_NAME = aws_dynamodb_table.shadow_events.name
    }
  }
}

# ─── compact Lambda（EventBridge Scheduler → raw/ 小ファイルの定期compaction） ───

resource "aws_lambda_function" "compact" {
  function_name    = "${var.project}-compact"
  filename         = data.archive_file.compact.output_path
  source_code_hash = data.archive_file.compact.output_base64sha256
  runtime          = "python3.12"
  handler          = "index.handler"
  role             = aws_iam_role.lambda_compact.arn
  timeout          = 900

  # 同一パーティションへの並行書き込みレース（マージ後・削除完了前に別実行が読みに来て
  # 二重カウントする）を構造的に防ぐため、同時実行数を1に固定する。スケジュール実行が
  # 手動実行（バックフィル等）と鉢合わせた場合はスロットリングされるが、EventBridge
  # Schedulerのデフォルトリトライで後追いされるため実害はない
  reserved_concurrent_executions = 1

  environment {
    variables = {
      S3_BUCKET              = aws_s3_bucket.main.bucket
      ARCHIVE_BUCKET         = aws_s3_bucket.archive.bucket
      LOOKBACK_HOURS         = "72"
      MAX_PARTITIONS_PER_RUN = "200"
    }
  }
}

# ─── API Gateway HTTP API ─────────────────────────────────────────────────────

resource "aws_apigatewayv2_api" "main" {
  name          = var.project
  protocol_type = "HTTP"

  cors_configuration {
    allow_origins = ["https://${local.web_domain}"]
    allow_methods = ["GET", "PUT", "DELETE", "POST", "OPTIONS"]
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
    audience = [
      aws_cognito_user_pool_client.web.id,
      aws_cognito_user_pool_client.mobile.id,
    ]
    issuer = "https://cognito-idp.${var.aws_region}.amazonaws.com/${aws_cognito_user_pool.main.id}"
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

resource "aws_apigatewayv2_integration" "labels" {
  api_id                 = aws_apigatewayv2_api.main.id
  integration_type       = "AWS_PROXY"
  integration_uri        = aws_lambda_function.labels.invoke_arn
  payload_format_version = "2.0"
}

resource "aws_apigatewayv2_integration" "status" {
  api_id                 = aws_apigatewayv2_api.main.id
  integration_type       = "AWS_PROXY"
  integration_uri        = aws_lambda_function.status.invoke_arn
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

resource "aws_apigatewayv2_route" "labels_get" {
  api_id             = aws_apigatewayv2_api.main.id
  route_key          = "GET /labels"
  target             = "integrations/${aws_apigatewayv2_integration.labels.id}"
  authorizer_id      = aws_apigatewayv2_authorizer.cognito.id
  authorization_type = "JWT"
}

resource "aws_apigatewayv2_route" "labels_put" {
  api_id             = aws_apigatewayv2_api.main.id
  route_key          = "PUT /labels"
  target             = "integrations/${aws_apigatewayv2_integration.labels.id}"
  authorizer_id      = aws_apigatewayv2_authorizer.cognito.id
  authorization_type = "JWT"
}

resource "aws_apigatewayv2_route" "status" {
  api_id             = aws_apigatewayv2_api.main.id
  route_key          = "GET /status"
  target             = "integrations/${aws_apigatewayv2_integration.status.id}"
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

resource "aws_lambda_permission" "apigw_labels" {
  statement_id  = "AllowAPIGWInvoke"
  action        = "lambda:InvokeFunction"
  function_name = aws_lambda_function.labels.function_name
  principal     = "apigateway.amazonaws.com"
  source_arn    = "${aws_apigatewayv2_api.main.execution_arn}/*/*"
}

resource "aws_lambda_permission" "apigw_status" {
  statement_id  = "AllowAPIGWInvoke"
  action        = "lambda:InvokeFunction"
  function_name = aws_lambda_function.status.function_name
  principal     = "apigateway.amazonaws.com"
  source_arn    = "${aws_apigatewayv2_api.main.execution_arn}/*/*"
}

resource "aws_lambda_permission" "apigw_admin" {
  statement_id  = "AllowAPIGWInvoke"
  action        = "lambda:InvokeFunction"
  function_name = aws_lambda_function.admin.function_name
  principal     = "apigateway.amazonaws.com"
  source_arn    = "${aws_apigatewayv2_api.main.execution_arn}/*/*"
}

# ─── admin Lambda（管理者専用: デバイス一覧・Shadow 更新・Job 発行・グループ管理） ─

resource "aws_lambda_function" "admin" {
  function_name    = "${var.project}-admin"
  filename         = data.archive_file.admin.output_path
  source_code_hash = data.archive_file.admin.output_base64sha256
  runtime          = "python3.12"
  handler          = "index.handler"
  role             = aws_iam_role.lambda_admin.arn
  timeout          = 30

  environment {
    variables = {
      IOT_ENDPOINT        = "https://${data.aws_iot_endpoint.main.endpoint_address}"
      ACCOUNT_ID          = data.aws_caller_identity.current.account_id
      SHADOW_EVENTS_TABLE = aws_dynamodb_table.shadow_events.name
    }
  }
}

resource "aws_apigatewayv2_integration" "admin" {
  api_id                 = aws_apigatewayv2_api.main.id
  integration_type       = "AWS_PROXY"
  integration_uri        = aws_lambda_function.admin.invoke_arn
  payload_format_version = "2.0"
}

# ANY を使うと OPTIONS（プリフライト）も JWT 認証にかかるため、メソッド別に分割する
resource "aws_apigatewayv2_route" "admin_get" {
  api_id             = aws_apigatewayv2_api.main.id
  route_key          = "GET /admin/{proxy+}"
  target             = "integrations/${aws_apigatewayv2_integration.admin.id}"
  authorizer_id      = aws_apigatewayv2_authorizer.cognito.id
  authorization_type = "JWT"
}

resource "aws_apigatewayv2_route" "admin_put" {
  api_id             = aws_apigatewayv2_api.main.id
  route_key          = "PUT /admin/{proxy+}"
  target             = "integrations/${aws_apigatewayv2_integration.admin.id}"
  authorizer_id      = aws_apigatewayv2_authorizer.cognito.id
  authorization_type = "JWT"
}

resource "aws_apigatewayv2_route" "admin_post" {
  api_id             = aws_apigatewayv2_api.main.id
  route_key          = "POST /admin/{proxy+}"
  target             = "integrations/${aws_apigatewayv2_integration.admin.id}"
  authorizer_id      = aws_apigatewayv2_authorizer.cognito.id
  authorization_type = "JWT"
}
