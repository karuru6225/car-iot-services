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
