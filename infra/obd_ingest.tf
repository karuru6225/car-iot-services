# ─── モバイルアプリ → POST /obd → S3 → Athena（OBD-IIデータ取り込み） ─────────
# 既存ingest（IoT Core経由）とは別経路。モバイル側でバッファ・バッチ化されたOBD
# データをHTTP経由で受け取り、NDJSONとしてS3へ書く。既存sensor_dataテーブル・
# raw/ 配下には一切影響しない、完全に独立した新規リソース一式。

locals {
  obd_ingest_src_dir = "${path.module}/lambda_src/obd_ingest"
}

data "archive_file" "obd_ingest" {
  type        = "zip"
  source_dir  = local.obd_ingest_src_dir
  output_path = "${local.build_dir}/obd_ingest.zip"
}

# ─── Lambda ───────────────────────────────────────────────────────────────────

resource "aws_lambda_function" "obd_ingest" {
  function_name    = "${var.project}-obd-ingest"
  filename         = data.archive_file.obd_ingest.output_path
  source_code_hash = data.archive_file.obd_ingest.output_base64sha256
  runtime          = "python3.12"
  handler          = "index.handler"
  role             = aws_iam_role.lambda_obd_ingest.arn
  timeout          = 15

  environment {
    variables = {
      S3_BUCKET = aws_s3_bucket.main.bucket
    }
  }
}

resource "aws_apigatewayv2_integration" "obd_ingest" {
  api_id                 = aws_apigatewayv2_api.main.id
  integration_type       = "AWS_PROXY"
  integration_uri        = aws_lambda_function.obd_ingest.invoke_arn
  payload_format_version = "2.0"
}

resource "aws_apigatewayv2_route" "obd_ingest" {
  api_id             = aws_apigatewayv2_api.main.id
  route_key          = "POST /obd"
  target             = "integrations/${aws_apigatewayv2_integration.obd_ingest.id}"
  authorizer_id      = aws_apigatewayv2_authorizer.cognito.id
  authorization_type = "JWT"
}

resource "aws_lambda_permission" "apigw_obd_ingest" {
  statement_id  = "AllowAPIGWInvoke"
  action        = "lambda:InvokeFunction"
  function_name = aws_lambda_function.obd_ingest.function_name
  principal     = "apigateway.amazonaws.com"
  source_arn    = "${aws_apigatewayv2_api.main.execution_arn}/*/*"
}

# ─── IAM ────────────────────────────────────────────────────────────────────

resource "aws_iam_role" "lambda_obd_ingest" {
  name               = "${var.project}-lambda-obd-ingest"
  assume_role_policy = data.aws_iam_policy_document.lambda_assume.json
}

resource "aws_iam_role_policy" "lambda_obd_ingest" {
  role = aws_iam_role.lambda_obd_ingest.id
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
        Effect   = "Allow"
        Action   = "s3:PutObject"
        Resource = "${aws_s3_bucket.main.arn}/obd/*"
      },
    ]
  })
}

# ─── Glue Table（obd_data、sensor_dataとは独立） ───────────────────────────────
# オブジェクトキー: obd/year=YYYY/month=MM/day=DD/hour=HH/{device_id}-{uuid}.ndjson

resource "aws_glue_catalog_table" "obd_data" {
  name          = "obd_data"
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
    "storage.location.template" = "s3://${aws_s3_bucket.main.bucket}/obd/year=$${year}/month=$${month}/day=$${day}/hour=$${hour}/"
    "classification"            = "json"
    "compressionType"           = "none"
  }

  storage_descriptor {
    location      = "s3://${aws_s3_bucket.main.bucket}/obd/"
    input_format  = "org.apache.hadoop.mapred.TextInputFormat"
    output_format = "org.apache.hadoop.hive.ql.io.HiveIgnoreKeyTextOutputFormat"

    ser_de_info {
      serialization_library = "org.openx.data.jsonserde.JsonSerDe"
      parameters = {
        "ignore.malformed.json" = "true"
      }
    }

    columns {
      name = "device_id"
      type = "string"
    }
    columns {
      name = "uploaded_by"
      type = "string"
    }
    columns {
      name = "ingest_ts"
      type = "string"
    }
    columns {
      name = "obd_ts"
      type = "bigint"
    }
    columns {
      name = "rpm"
      type = "int"
    }
    columns {
      name = "speed_kmh"
      type = "int"
    }
    columns {
      name = "load_pct"
      type = "int"
    }
    columns {
      name = "map_kpa"
      type = "int"
    }
    columns {
      name = "baro_kpa"
      type = "int"
    }
    columns {
      name = "boost_kpa"
      type = "int"
    }
    columns {
      name = "throttle_pct"
      type = "int"
    }
    columns {
      name = "timing_deg"
      type = "double"
    }
    columns {
      name = "ecu_voltage"
      type = "double"
    }
    columns {
      name = "maf_gs"
      type = "double"
    }
    columns {
      name = "coolant_c"
      type = "int"
    }
    columns {
      name = "fuel_rate_lph"
      type = "double"
    }
    columns {
      name = "stft_pct"
      type = "double"
    }
    columns {
      name = "ltft_pct"
      type = "double"
    }
    columns {
      name = "o2_b1s2_v"
      type = "double"
    }
    columns {
      name = "o2_b1s2_trim_pct"
      type = "double"
    }
    columns {
      name = "engine_run_time_sec"
      type = "int"
    }
    columns {
      name = "mil_distance_km"
      type = "int"
    }
    columns {
      name = "o2_s1_ratio"
      type = "double"
    }
    columns {
      name = "o2_s1_voltage"
      type = "double"
    }
    columns {
      name = "evap_purge_pct"
      type = "int"
    }
    columns {
      name = "warmups_since_cleared"
      type = "int"
    }
    columns {
      name = "distance_since_cleared_km"
      type = "int"
    }
    columns {
      name = "catalyst_temp_c"
      type = "double"
    }
    columns {
      name = "absolute_load_pct"
      type = "double"
    }
    columns {
      name = "commanded_afr"
      type = "double"
    }
    columns {
      name = "throttle_b_pct"
      type = "int"
    }
    columns {
      name = "accel_pedal_d_pct"
      type = "int"
    }
    columns {
      name = "accel_pedal_e_pct"
      type = "int"
    }
    columns {
      name = "fuel_type"
      type = "int"
    }
    columns {
      name = "sec_o2_trim_st_pct"
      type = "double"
    }
    columns {
      name = "sec_o2_trim_lt_pct"
      type = "double"
    }
    columns {
      name = "iat_c"
      type = "int"
    }
    columns {
      name = "iat2_c"
      type = "int"
    }
    columns {
      name = "valid"
      type = "boolean"
    }
    columns {
      name = "lat"
      type = "double"
    }
    columns {
      name = "lon"
      type = "double"
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
