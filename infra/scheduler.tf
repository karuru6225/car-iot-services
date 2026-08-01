# ─── compact Lambda 定期実行（EventBridge Scheduler） ─────────────────────────
# cron はデフォルトでUTC評価される（明示的なタイムゾーン指定はしない）。
# 毎時5分に実行し、compact Lambda側で「現在進行中のhour + 直前1hour」を
# 安全マージンとして除外した過去分を対象にする（infra/lambda_src/compact/index.py参照）。

resource "aws_scheduler_schedule" "compact" {
  name       = "${var.project}-compact"
  group_name = "default"

  flexible_time_window {
    mode = "OFF"
  }

  schedule_expression = "cron(5 * * * ? *)"

  target {
    arn      = aws_lambda_function.compact.arn
    role_arn = aws_iam_role.scheduler_compact.arn
  }
}
