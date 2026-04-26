# ─── Thing ────────────────────────────────────────────────────────────────────

resource "aws_iot_thing" "device" {
  name = var.device_id
}

# ─── X.509 証明書 ─────────────────────────────────────────────────────────────

resource "aws_iot_certificate" "device" {
  active = true
}

# 証明書・秘密鍵を Secrets Manager に保存（apply 後ここから取得してデバイスに書き込む）
resource "aws_secretsmanager_secret" "device_cert" {
  name                    = "${var.project}/${var.device_id}/cert"
  recovery_window_in_days = 0
}

resource "aws_secretsmanager_secret_version" "device_cert" {
  secret_id = aws_secretsmanager_secret.device_cert.id
  secret_string = jsonencode({
    certificate_pem = aws_iot_certificate.device.certificate_pem
    private_key     = aws_iot_certificate.device.private_key
  })
}

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
        ]
      },
    ]
  })
}

# ─── Thing / 証明書 / Policy のアタッチ ──────────────────────────────────────

resource "aws_iot_thing_principal_attachment" "device" {
  thing     = aws_iot_thing.device.name
  principal = aws_iot_certificate.device.arn
}

resource "aws_iot_policy_attachment" "device" {
  policy = aws_iot_policy.device.name
  target = aws_iot_certificate.device.arn
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
