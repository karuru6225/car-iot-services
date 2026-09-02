# ─── Shadow reported の変化イベント記録用テーブル ─────────────────────────────
# Device Shadow は最新値しか保持しないため、charging等のreportedフィールドが
# 「いつ・何から何に変わったか」を別途ここに記録する（詳細は shadow_events Lambda 参照）。
# TTLは設定せず無期限保持（raw/・rollup/ と同方針）。

resource "aws_dynamodb_table" "shadow_events" {
  name         = "${var.project}-shadow-events"
  billing_mode = "PAY_PER_REQUEST"
  hash_key     = "device_id"
  range_key    = "ts"

  attribute {
    name = "device_id"
    type = "S"
  }

  attribute {
    name = "ts"
    type = "N" # epoch秒（shadow update/documentsメッセージのtimestampをそのまま使用）
  }
}
