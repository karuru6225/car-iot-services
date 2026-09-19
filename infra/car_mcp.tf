# ─── car_mcp（個人用AIアシスタントmnemosyne向け 読み取り専用MCPサーバ）用IAMユーザー ───
# MCPサーバはLambdaではなくmnemosyneのdocker compose上で常駐するため、実行ロールを
# 引き受けられない。専用のIAMユーザーを作り、アクセスキーをコンテナに渡す。
#
# 権限は「会話中に車のデータを読む」ことに必要な範囲だけに絞る:
#   - Athena: ワークグループでのクエリ実行と結果取得（書き込み系・削除系は無し）
#   - Glue: 参照するテーブルの定義の読み取り
#   - S3: raw/・obd/・rollup/・trip-analysis/の読み取りと、Athena結果置き場への書き込み
#   - IoT: Shadowの読み取りのみ（desiredの書き換え・Jobs発行はさせない）
# 車に作用する権限は一切付けない（HANDOFF_mnemosyne.md 1章: 車に作用する道具はそもそも作らない）。
#
# アクセスキーはTerraformで作らない。aws_iam_access_keyで作ると秘密鍵がtfstateに
# 平文で残るため、apply後にCLIで発行する（手順はcar_mcp/README.md）。

resource "aws_iam_user" "car_mcp" {
  name = "${var.project}-car-mcp"
}

resource "aws_iam_user_policy" "car_mcp" {
  name = "${var.project}-car-mcp-readonly"
  user = aws_iam_user.car_mcp.name
  policy = jsonencode({
    Version = "2012-10-17"
    Statement = [
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
          "arn:aws:glue:${var.aws_region}:${data.aws_caller_identity.current.account_id}:table/${local.glue_db_name}/obd_data",
          "arn:aws:glue:${var.aws_region}:${data.aws_caller_identity.current.account_id}:table/${local.glue_db_name}/battery_rollup",
        ]
      },
      {
        # データの読み取り。Athenaはクエリ対象のオブジェクトを呼び出し元の権限で読む
        Effect = "Allow"
        Action = ["s3:GetObject"]
        Resource = [
          "${aws_s3_bucket.main.arn}/raw/*",
          "${aws_s3_bucket.main.arn}/obd/*",
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
            "s3:prefix" = ["raw/*", "obd/*", "rollup/*", "trip-analysis/*", "athena-results/*"]
          }
        }
      },
      {
        # GetBucketLocationはprefix条件を持たないリクエストなので、条件なしで別に許可する
        Effect   = "Allow"
        Action   = "s3:GetBucketLocation"
        Resource = aws_s3_bucket.main.arn
      },
      {
        Effect   = "Allow"
        Action   = "iot:GetThingShadow"
        Resource = "arn:aws:iot:${var.aws_region}:${data.aws_caller_identity.current.account_id}:thing/esp32-gw-*"
      },
    ]
  })
}
