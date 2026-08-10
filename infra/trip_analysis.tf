# ─── トリップ終了検知 → AI分析パイプライン ─────────────────────────────────────
# obd_data（obd_ingest.tf）とは独立した新規リソース一式。デバイスごとの最終受信
# 状態をDynamoDBで追跡し、走行セッション終了を検知したらAthenaで集計してBedrockに
# ナラティブ分析を書かせ、trip-summaryテーブルへ蓄積する。
#
# トリップ終了は次のいずれかで判定する（trip_sweep Lambda側、PR2で実装）:
#   (a) valid=falseへ転じてから5秒（obd_uploader.dartが尾流し+即flushする、
#       mobile/lib/services/obd_uploader.dart参照）
#   (b) 直近データから10分無音（旧アプリ・アップロード失敗時のフォールバック）
#
# device-watermarkテーブルはデバイスごとに1アイテムのみを保持し、obd_ingest Lambda
# が書き込みのたびに上書きする。open_marker属性は「このセッションがまだtrip_sweepに
# 処理されていない」間だけ"OPEN"を持ち、処理済みになったらREMOVEする。DynamoDBの
# GSIは対象属性を持たないアイテムをインデックスに載せないため、open-index GSIには
# 未処理セッションだけが並ぶ。trip_sweep Lambdaは全件Scanではなく、このGSIへの
# Queryだけで済む（デバイス数が増えてもコスト・レイテンシが変わらない）。

resource "aws_dynamodb_table" "device_watermark" {
  name         = "${var.project}-device-watermark"
  billing_mode = "PAY_PER_REQUEST"
  hash_key     = "device_id"

  attribute {
    name = "device_id"
    type = "S"
  }

  # スパースGSI用。session_closed（未処理）の間だけこの属性を持つアイテムがある
  attribute {
    name = "open_marker"
    type = "S"
  }

  global_secondary_index {
    name            = "open-index"
    hash_key        = "open_marker"
    projection_type = "ALL"
  }
}

# device_id + session_start（トリップ開始obd_ts）で1トリップ1アイテム。
# narrativeはBedrock生成テキスト（PR3で追記、PR2時点では空文字のまま）。
resource "aws_dynamodb_table" "trip_summary" {
  name         = "${var.project}-trip-summary"
  billing_mode = "PAY_PER_REQUEST"
  hash_key     = "device_id"
  range_key    = "session_start"

  attribute {
    name = "device_id"
    type = "S"
  }

  attribute {
    name = "session_start"
    type = "N"
  }
}

# ─── trip_sweep Lambda（EventBridge Schedulerで1分ごとに実行） ────────────────
# open-index GSIをQueryしてトリップ終了を検知し、Athenaでobd_dataを集計して
# trip-summaryへ保存する（infra/lambda_src/trip_sweep/index.py参照）。

locals {
  trip_sweep_src_dir = "${path.module}/lambda_src/trip_sweep"
}

data "archive_file" "trip_sweep" {
  type        = "zip"
  source_dir  = local.trip_sweep_src_dir
  output_path = "${local.build_dir}/trip_sweep.zip"
  excludes    = ["tests"]
}

resource "aws_lambda_function" "trip_sweep" {
  function_name    = "${var.project}-trip-sweep"
  filename         = data.archive_file.trip_sweep.output_path
  source_code_hash = data.archive_file.trip_sweep.output_base64sha256
  runtime          = "python3.12"
  handler          = "index.handler"
  role             = aws_iam_role.lambda_trip_sweep.arn
  timeout          = 120

  environment {
    variables = {
      WATERMARK_TABLE    = aws_dynamodb_table.device_watermark.name
      TRIP_SUMMARY_TABLE = aws_dynamodb_table.trip_summary.name
      ATHENA_DATABASE    = local.glue_db_name
      ATHENA_WORKGROUP   = aws_athena_workgroup.main.name
    }
  }
}

resource "aws_iam_role" "lambda_trip_sweep" {
  name               = "${var.project}-lambda-trip-sweep"
  assume_role_policy = data.aws_iam_policy_document.lambda_assume.json
}

resource "aws_iam_role_policy" "lambda_trip_sweep" {
  role = aws_iam_role.lambda_trip_sweep.id
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
        # open-index GSIのQueryのみ（全件Scanは不要な設計、infra/trip_analysis.tf冒頭コメント参照）
        Effect   = "Allow"
        Action   = "dynamodb:Query"
        Resource = "${aws_dynamodb_table.device_watermark.arn}/index/open-index"
      },
      {
        Effect   = "Allow"
        Action   = "dynamodb:UpdateItem"
        Resource = aws_dynamodb_table.device_watermark.arn
      },
      {
        Effect   = "Allow"
        Action   = "dynamodb:PutItem"
        Resource = aws_dynamodb_table.trip_summary.arn
      },
      {
        # Athena クエリ実行（query Lambdaのロールと同型）
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
        # Glue メタデータ参照（Athena がobd_dataのテーブル定義を読む）
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
        # obd_data本体の読み取り + Athenaクエリ結果の読み書き（athena-results/配下）
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
    ]
  })
}

resource "aws_scheduler_schedule" "trip_sweep" {
  name       = "${var.project}-trip-sweep"
  group_name = "default"

  flexible_time_window {
    mode = "OFF"
  }

  schedule_expression = "rate(1 minute)"

  target {
    arn      = aws_lambda_function.trip_sweep.arn
    role_arn = aws_iam_role.scheduler_trip_sweep.arn
  }
}

resource "aws_iam_role" "scheduler_trip_sweep" {
  name = "${var.project}-scheduler-trip-sweep"
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

resource "aws_iam_role_policy" "scheduler_trip_sweep" {
  role = aws_iam_role.scheduler_trip_sweep.id
  policy = jsonencode({
    Version = "2012-10-17"
    Statement = [{
      Effect   = "Allow"
      Action   = "lambda:InvokeFunction"
      Resource = aws_lambda_function.trip_sweep.arn
    }]
  })
}
