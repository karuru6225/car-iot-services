locals {
  # Glue/Athena はハイフン不可なのでアンダースコアに変換
  glue_db_name = replace(var.project, "-", "_")
}

# ─── S3 バケット ──────────────────────────────────────────────────────────────

resource "aws_s3_bucket" "main" {
  # グローバルユニークを保証するためアカウントIDをサフィックスに付加
  bucket = "${var.project}-${data.aws_caller_identity.current.account_id}"
}

resource "aws_s3_bucket_public_access_block" "main" {
  bucket                  = aws_s3_bucket.main.id
  block_public_acls       = true
  block_public_policy     = true
  ignore_public_acls      = true
  restrict_public_buckets = true
}

resource "aws_s3_bucket_server_side_encryption_configuration" "main" {
  bucket = aws_s3_bucket.main.id
  rule {
    apply_server_side_encryption_by_default {
      sse_algorithm = "AES256"
    }
  }
}

# ─── Glue Database ────────────────────────────────────────────────────────────

resource "aws_glue_catalog_database" "main" {
  name = local.glue_db_name
}

# ─── Glue Table（パーティションプロジェクション付き） ─────────────────────────
# オブジェクトキー: raw/year=YYYY/month=MM/day=DD/hour=HH/{device_id}-{uuid}.json

resource "aws_glue_catalog_table" "sensor_data" {
  name          = "sensor_data"
  database_name = aws_glue_catalog_database.main.name
  table_type    = "EXTERNAL_TABLE"

  parameters = {
    "projection.enabled"        = "true"
    "projection.year.type"      = "integer"
    "projection.year.range"     = "2026,2036"
    "projection.month.type"     = "integer"
    "projection.month.range"    = "1,12"
    "projection.month.digits"   = "2"
    "projection.day.type"       = "integer"
    "projection.day.range"      = "1,31"
    "projection.day.digits"     = "2"
    "projection.hour.type"      = "integer"
    "projection.hour.range"     = "0,23"
    "projection.hour.digits"    = "2"
    "storage.location.template" = "s3://${aws_s3_bucket.main.bucket}/raw/year=$${year}/month=$${month}/day=$${day}/hour=$${hour}/"
    "classification"            = "json"
    "compressionType"           = "none"
  }

  storage_descriptor {
    location      = "s3://${aws_s3_bucket.main.bucket}/raw/"
    input_format  = "org.apache.hadoop.mapred.TextInputFormat"
    output_format = "org.apache.hadoop.hive.ql.io.HiveIgnoreKeyTextOutputFormat"

    ser_de_info {
      serialization_library = "org.openx.data.jsonserde.JsonSerDe"
      parameters = {
        "ignore.malformed.json" = "true"
      }
    }

    columns {
      name = "ts"
      type = "string"
    }
    columns {
      name = "device_id"
      type = "string"
    }
    columns {
      name = "sensor_type"
      type = "string"
    }
    columns {
      name = "voltage"
      type = "double"
    }
    columns {
      name = "id"
      type = "string"
    }
    columns {
      name = "addr"
      type = "string"
    }
    columns {
      name = "temp"
      type = "double"
    }
    columns {
      name = "humidity"
      type = "int"
    }
    columns {
      name = "battery"
      type = "int"
    }
    columns {
      name = "rssi"
      type = "int"
    }
  }

  partition_keys {
    name = "year"
    type = "string"
  }
  partition_keys {
    name = "month"
    type = "string"
  }
  partition_keys {
    name = "day"
    type = "string"
  }
  partition_keys {
    name = "hour"
    type = "string"
  }
}

# ─── Athena Workgroup ──────────────────────────────────────────────────────────

resource "aws_s3_bucket_lifecycle_configuration" "main" {
  bucket = aws_s3_bucket.main.id

  rule {
    id     = "delete-athena-results"
    status = "Enabled"
    filter {
      prefix = "athena-results/"
    }
    expiration {
      days = 2
    }
  }
}

resource "aws_athena_workgroup" "main" {
  name = var.project

  configuration {
    result_configuration {
      output_location = "s3://${aws_s3_bucket.main.bucket}/athena-results/"
    }
    enforce_workgroup_configuration    = true
    publish_cloudwatch_metrics_enabled = false
  }
}
