# ─── OBDトリップ分析（手動トリガー・S3ベース） ────────────────────────────────
# 管理画面から手動で「分析開始」を押したときだけ、未分析期間のOBDデータをAthenaで
# 取得しトリップに分割・集計してS3へ保存する。DynamoDB等の可変ステートは持たず、
# 「どこまで分析済みか」はS3に保存済みのトリップ結果一覧から逆算する
# （旧DynamoDB watermark設計、旧PR #28・#34は競合バグのためclose済み）。
#
# API Gatewayの29秒固定タイムアウト制約を避けるため、POST /trip-analysisは
# ジョブを作成して自分自身をLambda self-invoke（InvocationType="Event"）した
# 直後に返る。重い処理（Athenaクエリ〜集計〜複数S3書き込み）はself-invoke側で
# 最大600秒まで自由に使う。

locals {
  trip_analysis_src_dir   = "${path.module}/lambda_src/trip_analysis"
  trip_analysis_func_name = "${var.project}-trip-analysis"
}

data "archive_file" "trip_analysis" {
  type        = "zip"
  source_dir  = local.trip_analysis_src_dir
  output_path = "${local.build_dir}/trip_analysis.zip"
  excludes    = ["tests"]
}

resource "aws_lambda_function" "trip_analysis" {
  function_name    = local.trip_analysis_func_name
  filename         = data.archive_file.trip_analysis.output_path
  source_code_hash = data.archive_file.trip_analysis.output_base64sha256
  runtime          = "python3.12"
  handler          = "index.handler"
  role             = aws_iam_role.lambda_trip_analysis.arn
  timeout          = 600

  environment {
    variables = {
      S3_BUCKET             = aws_s3_bucket.main.bucket
      ATHENA_DATABASE       = local.glue_db_name
      ATHENA_WORKGROUP      = aws_athena_workgroup.main.name
      SELF_FUNCTION_NAME    = local.trip_analysis_func_name
      GAP_TIMEOUT_SEC       = "600"
      MIN_TRIP_DURATION_SEC = "30"
    }
  }
}

resource "aws_apigatewayv2_integration" "trip_analysis" {
  api_id                 = aws_apigatewayv2_api.main.id
  integration_type       = "AWS_PROXY"
  integration_uri        = aws_lambda_function.trip_analysis.invoke_arn
  payload_format_version = "2.0"
}

resource "aws_apigatewayv2_route" "trip_analysis_get" {
  api_id             = aws_apigatewayv2_api.main.id
  route_key          = "GET /trip-analysis"
  target             = "integrations/${aws_apigatewayv2_integration.trip_analysis.id}"
  authorizer_id      = aws_apigatewayv2_authorizer.cognito.id
  authorization_type = "JWT"
}

resource "aws_apigatewayv2_route" "trip_analysis_post" {
  api_id             = aws_apigatewayv2_api.main.id
  route_key          = "POST /trip-analysis"
  target             = "integrations/${aws_apigatewayv2_integration.trip_analysis.id}"
  authorizer_id      = aws_apigatewayv2_authorizer.cognito.id
  authorization_type = "JWT"
}

resource "aws_lambda_permission" "apigw_trip_analysis" {
  statement_id  = "AllowAPIGWInvoke"
  action        = "lambda:InvokeFunction"
  function_name = aws_lambda_function.trip_analysis.function_name
  principal     = "apigateway.amazonaws.com"
  source_arn    = "${aws_apigatewayv2_api.main.execution_arn}/*/*"
}

# ─── IAM ────────────────────────────────────────────────────────────────────

resource "aws_iam_role" "lambda_trip_analysis" {
  name               = "${var.project}-lambda-trip-analysis"
  assume_role_policy = data.aws_iam_policy_document.lambda_assume.json
}

resource "aws_iam_role_policy" "lambda_trip_analysis" {
  role = aws_iam_role.lambda_trip_analysis.id
  policy = jsonencode({
    Version = "2012-10-17"
    Statement = [
      {
        Effect = "Allow"
        Action = [
          "logs:CreateLogGroup",
          "logs:CreateLogStream",
          "logs:PutLogEvents",
        ]
        Resource = "arn:aws:logs:*:*:*"
      },
      {
        # Athena クエリ実行
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
        # Glue メタデータ参照（obd_dataテーブルのみにスコープ）
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
          "arn:aws:glue:${var.aws_region}:${data.aws_caller_identity.current.account_id}:table/${local.glue_db_name}/obd_data",
        ]
      },
      {
        # S3: obd/読み取り、trip-analysis/・trip-analysis-jobs/・athena-results/読み書き
        Effect = "Allow"
        Action = [
          "s3:GetObject",
          "s3:PutObject",
          "s3:GetBucketLocation",
          "s3:ListBucket",
        ]
        Resource = [
          aws_s3_bucket.main.arn,
          "${aws_s3_bucket.main.arn}/*",
        ]
      },
      {
        # self-invoke（重い処理を非同期委譲するため）。同一関数を呼ぶだけなのでIAMの
        # identity-basedポリシーで十分（resource-basedのaws_lambda_permissionは不要）。
        # 循環依存を避けるため関数名をローカル変数からリテラル組み立てする
        Effect   = "Allow"
        Action   = "lambda:InvokeFunction"
        Resource = "arn:aws:lambda:${var.aws_region}:${data.aws_caller_identity.current.account_id}:function:${local.trip_analysis_func_name}"
      },
    ]
  })
}
