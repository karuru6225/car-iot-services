# ─── バッテリー充放電量 日次rollupバッチ（EventBridge Scheduler起動） ───────────
# 別リポジトリclaude-opentelemetryのGrafanaダッシュボードが「当日・前日を除く長期間」の
# 日次充電量・放電量を常時表示ダッシュボードで見られるようにするための事前集計バッチ。
# Grafanaの自動リフレッシュのたびにAthenaへオンデマンドで長期間クエリを投げるとコストが
# 期間の伸びに比例して増えるため、毎日1回このLambdaが日次集計を計算しS3のrollup/へ
# 書き出しておき、Grafana（Infinityデータソース、claude-opentelemetry/infra/grafana_infinity.tf）
# はそれを読むだけにする。詳細はinfra/lambda_src/battery_rollup/index.pyのdocstring参照。

locals {
  battery_rollup_src_dir   = "${path.module}/lambda_src/battery_rollup"
  battery_rollup_func_name = "${var.project}-battery-rollup"
}

data "archive_file" "battery_rollup" {
  type        = "zip"
  source_dir  = local.battery_rollup_src_dir
  output_path = "${local.build_dir}/battery_rollup.zip"
  excludes    = ["tests"]
}

resource "aws_lambda_function" "battery_rollup" {
  function_name    = local.battery_rollup_func_name
  filename         = data.archive_file.battery_rollup.output_path
  source_code_hash = data.archive_file.battery_rollup.output_base64sha256
  runtime          = "python3.12"
  handler          = "index.handler"
  role             = aws_iam_role.lambda_battery_rollup.arn
  timeout          = 900 # AWS Lambda上限。初回バックフィルのAthenaクエリ・S3書き込みの余裕を確保する

  environment {
    variables = {
      S3_BUCKET               = aws_s3_bucket.main.bucket
      ATHENA_DATABASE         = local.glue_db_name
      ATHENA_WORKGROUP        = aws_athena_workgroup.main.name
      REPROCESS_LOOKBACK_DAYS = "3"
      MAX_DAYS_PER_RUN        = "30"
      ATHENA_POLL_TIMEOUT_SEC = "600"
    }
  }
}

# cronはデフォルトでUTC評価される。毎日 UTC 16:00（JST 01:00）に実行し、前日(JST)分の
# データが十分確定した時刻を狙う（REPROCESS_LOOKBACK_DAYSによる再検証もあるため厳密な
# 時刻選定はさほど重要ではない）。
resource "aws_scheduler_schedule" "battery_rollup" {
  name       = "${var.project}-battery-rollup"
  group_name = "default"

  flexible_time_window {
    mode = "OFF"
  }

  schedule_expression = "cron(0 16 * * ? *)"

  target {
    arn      = aws_lambda_function.battery_rollup.arn
    role_arn = aws_iam_role.scheduler_battery_rollup.arn
  }
}

# ─── IAM ────────────────────────────────────────────────────────────────────

resource "aws_iam_role" "lambda_battery_rollup" {
  name               = "${var.project}-lambda-battery-rollup"
  assume_role_policy = data.aws_iam_policy_document.lambda_assume.json
}

resource "aws_iam_role_policy" "lambda_battery_rollup" {
  role = aws_iam_role.lambda_battery_rollup.id
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
        # Glue メタデータ参照（sensor_dataテーブルのみにスコープ）
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
        # raw/（_earliest_battery_date用のドリルダウン）・athena-results/・rollup/ のみに限定
        Effect   = "Allow"
        Action   = "s3:ListBucket"
        Resource = aws_s3_bucket.main.arn
        Condition = {
          StringLike = {
            "s3:prefix" = ["raw/*", "athena-results/*", "rollup/*"]
          }
        }
      },
      {
        Effect   = "Allow"
        Action   = "s3:GetObject"
        Resource = "${aws_s3_bucket.main.arn}/raw/*"
      },
      {
        Effect   = "Allow"
        Action   = ["s3:GetObject", "s3:PutObject"]
        Resource = "${aws_s3_bucket.main.arn}/athena-results/*"
      },
      {
        # watermark逆算のための読み取り＋日次ファイル書き込み
        Effect   = "Allow"
        Action   = ["s3:GetObject", "s3:PutObject"]
        Resource = "${aws_s3_bucket.main.arn}/rollup/*"
      },
      {
        # Athenaがクエリ結果の出力先バケットを検証する際に使う（prefix条件では
        # 効かないバケットレベルのアクション。query/index.py・trip_analysis/index.pyの
        # IAMポリシーにも同様に付与されている）
        Effect   = "Allow"
        Action   = "s3:GetBucketLocation"
        Resource = aws_s3_bucket.main.arn
      },
    ]
  })
}

# ─── EventBridge Scheduler → battery_rollup Lambda 起動ロール ─────────────────

resource "aws_iam_role" "scheduler_battery_rollup" {
  name = "${var.project}-scheduler-battery-rollup"
  assume_role_policy = jsonencode({
    Version = "2012-10-17"
    Statement = [{
      Effect = "Allow"
      Action = "sts:AssumeRole"
      Principal = {
        Service = "scheduler.amazonaws.com"
      }
    }]
  })
}

resource "aws_iam_role_policy" "scheduler_battery_rollup" {
  role = aws_iam_role.scheduler_battery_rollup.id
  policy = jsonencode({
    Version = "2012-10-17"
    Statement = [{
      Effect   = "Allow"
      Action   = "lambda:InvokeFunction"
      Resource = aws_lambda_function.battery_rollup.arn
    }]
  })
}
