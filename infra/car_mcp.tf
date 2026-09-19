# ─── car_mcp（個人用AIアシスタントmnemosyne向け 車両データ読み取り専用MCPサーバ） ───
# mnemosyneのorchestratorがMCPクライアントとして POST /mcp を呼ぶ。詳細は
# infra/lambda_src/car_mcp/index.py と HANDOFF_mnemosyne.md。
#
# 認証の流れ:
#   1. mnemosyneが専用のCognitoアプリクライアント（client_credentials）でアクセストークンを取る
#   2. API Gatewayの専用JWT Authorizerが、そのクライアントのトークンだけを受け付ける
#      （既存のcognito Authorizerは使わない。Web/モバイルのトークンで /mcp を呼べず、
#       mnemosyneのトークンで /data 等の既存ルートを呼べない、を両方成り立たせるため）
#   3. ルートにスコープ car-mcp/read を要求する
# AWSのデータはLambdaの実行ロールで読む。mnemosyne側にAWSの認証情報は置かない。
#
# 実行ロールに付けるのは読み取りだけ。車に作用する権限（Shadowの書き換え・Jobs発行）は付けない
# （HANDOFF_mnemosyne.md 1章: 車に作用する道具はそもそも作らない）。

locals {
  car_mcp_src_dir   = "${path.module}/lambda_src/car_mcp"
  car_mcp_func_name = "${var.project}-car-mcp"
  car_mcp_scope     = "${aws_cognito_resource_server.car_mcp.identifier}/read"
}

# ─── Cognito: mnemosyne用のM2Mクライアント ─────────────────────────────────────

resource "aws_cognito_resource_server" "car_mcp" {
  identifier   = "car-mcp"
  name         = "car-mcp"
  user_pool_id = aws_cognito_user_pool.main.id

  scope {
    scope_name        = "read"
    scope_description = "車両データの読み取り"
  }
}

resource "aws_cognito_user_pool_client" "car_mcp" {
  name         = "${var.project}-car-mcp-mnemosyne"
  user_pool_id = aws_cognito_user_pool.main.id

  generate_secret                      = true
  allowed_oauth_flows                  = ["client_credentials"]
  allowed_oauth_scopes                 = [local.car_mcp_scope]
  allowed_oauth_flows_user_pool_client = true

  token_validity_units {
    access_token = "hours"
  }
  access_token_validity = 1
}

# ─── Lambda ───────────────────────────────────────────────────────────────────

data "archive_file" "car_mcp" {
  type        = "zip"
  source_dir  = local.car_mcp_src_dir
  output_path = "${local.build_dir}/car_mcp.zip"
  excludes    = ["tests"]
}

resource "aws_iam_role" "lambda_car_mcp" {
  name               = "${var.project}-lambda-car-mcp"
  assume_role_policy = data.aws_iam_policy_document.lambda_assume.json
}

resource "aws_iam_role_policy" "lambda_car_mcp" {
  role = aws_iam_role.lambda_car_mcp.id
  policy = jsonencode({
    Version = "2012-10-17"
    Statement = [
      {
        Effect   = "Allow"
        Action   = ["logs:CreateLogGroup", "logs:CreateLogStream", "logs:PutLogEvents"]
        Resource = "arn:aws:logs:*:*:*"
      },
      {
        Effect = "Allow"
        Action = [
          "athena:StartQueryExecution",
          "athena:GetQueryExecution",
          "athena:GetQueryResults",
          "athena:StopQueryExecution",
          "athena:GetWorkGroup",
        ]
        Resource = aws_athena_workgroup.main.arn
      },
      {
        Effect = "Allow"
        Action = [
          "glue:GetDatabase",
          "glue:GetTable",
          "glue:GetPartitions",
          "glue:BatchGetPartition",
        ]
        Resource = [
          "arn:aws:glue:${var.aws_region}:${data.aws_caller_identity.current.account_id}:catalog",
          aws_glue_catalog_database.main.arn,
          "arn:aws:glue:${var.aws_region}:${data.aws_caller_identity.current.account_id}:table/${local.glue_db_name}/sensor_data",
        ]
      },
      {
        # データの読み取り。Athenaはクエリ対象のオブジェクトを呼び出し元の権限で読む
        Effect = "Allow"
        Action = ["s3:GetObject"]
        Resource = [
          "${aws_s3_bucket.main.arn}/raw/*",
          "${aws_s3_bucket.main.arn}/rollup/*",
          "${aws_s3_bucket.main.arn}/trip-analysis/*",
        ]
      },
      {
        # Athenaのクエリ結果置き場（ワークグループのoutput_location）
        Effect = "Allow"
        Action = [
          "s3:GetObject",
          "s3:PutObject",
          "s3:AbortMultipartUpload",
          "s3:ListMultipartUploadParts",
        ]
        Resource = "${aws_s3_bucket.main.arn}/athena-results/*"
      },
      {
        Effect   = "Allow"
        Action   = "s3:ListBucket"
        Resource = aws_s3_bucket.main.arn
        Condition = {
          StringLike = {
            "s3:prefix" = ["raw/*", "rollup/*", "trip-analysis/*", "athena-results/*"]
          }
        }
      },
      {
        Effect   = "Allow"
        Action   = "s3:GetBucketLocation"
        Resource = aws_s3_bucket.main.arn
      },
      {
        Effect   = "Allow"
        Action   = "iot:GetThingShadow"
        Resource = "arn:aws:iot:${var.aws_region}:${data.aws_caller_identity.current.account_id}:thing/${var.car_mcp_device_id}"
      },
    ]
  })
}

resource "aws_lambda_function" "car_mcp" {
  function_name    = local.car_mcp_func_name
  filename         = data.archive_file.car_mcp.output_path
  source_code_hash = data.archive_file.car_mcp.output_base64sha256
  runtime          = "python3.12"
  handler          = "index.handler"
  role             = aws_iam_role.lambda_car_mcp.arn
  # API Gatewayの統合タイムアウト（30秒）より短くする。Athenaの待ちは20秒で打ち切る（carmcp/aws.py）
  timeout     = 28
  memory_size = 256

  environment {
    variables = {
      CAR_DEVICE_ID    = var.car_mcp_device_id
      S3_BUCKET        = aws_s3_bucket.main.bucket
      ATHENA_DATABASE  = aws_glue_catalog_database.main.name
      ATHENA_WORKGROUP = aws_athena_workgroup.main.name
      IOT_ENDPOINT     = "https://${data.aws_iot_endpoint.main.endpoint_address}"
      EXPOSE_LOCATION  = var.car_mcp_expose_location ? "true" : "false"
    }
  }
}

# ─── API Gateway ──────────────────────────────────────────────────────────────

resource "aws_apigatewayv2_authorizer" "car_mcp" {
  api_id           = aws_apigatewayv2_api.main.id
  authorizer_type  = "JWT"
  name             = "${var.project}-car-mcp"
  identity_sources = ["$request.header.Authorization"]

  jwt_configuration {
    # client_credentialsのアクセストークンにはaudが無く、API Gatewayはその場合client_idを照合する
    audience = [aws_cognito_user_pool_client.car_mcp.id]
    issuer   = "https://cognito-idp.${var.aws_region}.amazonaws.com/${aws_cognito_user_pool.main.id}"
  }
}

resource "aws_apigatewayv2_integration" "car_mcp" {
  api_id                 = aws_apigatewayv2_api.main.id
  integration_type       = "AWS_PROXY"
  integration_uri        = aws_lambda_function.car_mcp.invoke_arn
  payload_format_version = "2.0"
  timeout_milliseconds   = 30000
}

# MCPのStreamable HTTPはPOSTのほかにGET（SSE）・DELETE（セッション終了）も使いうる。
# このサーバはどちらも提供しないが、仕様どおり405を返せるようLambdaまで通す
resource "aws_apigatewayv2_route" "car_mcp" {
  api_id               = aws_apigatewayv2_api.main.id
  route_key            = "ANY /mcp"
  target               = "integrations/${aws_apigatewayv2_integration.car_mcp.id}"
  authorizer_id        = aws_apigatewayv2_authorizer.car_mcp.id
  authorization_type   = "JWT"
  authorization_scopes = [local.car_mcp_scope]
}

resource "aws_lambda_permission" "apigw_car_mcp" {
  statement_id  = "AllowAPIGWInvoke"
  action        = "lambda:InvokeFunction"
  function_name = aws_lambda_function.car_mcp.function_name
  principal     = "apigateway.amazonaws.com"
  source_arn    = "${aws_apigatewayv2_api.main.execution_arn}/*/*/mcp"
}
