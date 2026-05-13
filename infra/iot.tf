# ─── IoT Policy ───────────────────────────────────────────────────────────────

resource "aws_iot_policy" "device" {
  # デバイス共通ポリシー（MAC ベースの device ID に対応するためワイルドカードを使用）
  name = "${var.project}-device"
  policy = jsonencode({
    Version = "2012-10-17"
    Statement = [
      {
        Effect   = "Allow"
        Action   = "iot:Connect"
        Resource = "arn:aws:iot:${var.aws_region}:*:client/*"
      },
      {
        Effect = "Allow"
        Action = "iot:Publish"
        Resource = [
          "arn:aws:iot:${var.aws_region}:*:topic/sensors/*/data",
          "arn:aws:iot:${var.aws_region}:*:topic/$aws/things/*/shadow/update",
          "arn:aws:iot:${var.aws_region}:*:topic/$aws/things/*/jobs/$next/get",
          "arn:aws:iot:${var.aws_region}:*:topic/$aws/things/*/jobs/*/update",
        ]
      },
      {
        Effect = "Allow"
        Action = ["iot:Subscribe", "iot:Receive"]
        Resource = [
          "arn:aws:iot:${var.aws_region}:*:topicfilter/commands/*",
          "arn:aws:iot:${var.aws_region}:*:topic/commands/*",
          "arn:aws:iot:${var.aws_region}:*:topicfilter/$aws/things/*/jobs/*",
          "arn:aws:iot:${var.aws_region}:*:topic/$aws/things/*/jobs/*",
          "arn:aws:iot:${var.aws_region}:*:topicfilter/$aws/things/*/shadow/update/delta",
          "arn:aws:iot:${var.aws_region}:*:topic/$aws/things/*/shadow/update/delta",
        ]
      },
    ]
  })
}

# ─── Thing Group ──────────────────────────────────────────────────────────────

resource "aws_iot_thing_group" "esp32_gw" {
  name = "esp32-gw"
}

resource "aws_iot_thing_group_membership" "device" {
  thing_name       = aws_iot_thing.device.name
  thing_group_name = aws_iot_thing_group.esp32_gw.name
}

# ─── Topic Rule: sensors/+/data → Lambda ingest ───────────────────────────────

resource "aws_iot_topic_rule" "ingest" {
  # ルール名にハイフンは使えないので置換する
  name        = replace("${var.project}_ingest", "-", "_")
  enabled     = true
  sql         = "SELECT *, topic(2) AS device_id FROM 'sensors/+/data'"
  sql_version = "2016-03-23"

  lambda {
    function_arn = aws_lambda_function.ingest.arn
  }

  error_action {
    cloudwatch_logs {
      log_group_name = "/aws/iot/${var.project}/rule-errors"
      role_arn       = aws_iam_role.iot_error_logs.arn
    }
  }
}

resource "aws_lambda_permission" "iot_ingest" {
  statement_id  = "AllowIoTInvoke"
  action        = "lambda:InvokeFunction"
  function_name = aws_lambda_function.ingest.function_name
  principal     = "iot.amazonaws.com"
  source_arn    = aws_iot_topic_rule.ingest.arn
}

