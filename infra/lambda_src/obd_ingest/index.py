"""
モバイルアプリ → API Gateway（Cognito JWT Authorizer） → obd_ingest Lambda

受け取るリクエストボディ:
  {
    "device_id": "car-iot-xxxxxx",
    "readings": [
      {"ts": 1735689600, "rpm": 1800, "speedKmh": 45, ... ObdReadingの全フィールド},
      ...
    ]
  }

readings[].ts（epoch秒）でパーティション（year/month/day/hour）を決める。
ingest LambdaのS3書き込み時刻ではなく各readingの発生時刻でパーティションを分けるのは、
バッチ（最大100件/5分）がパーティション境界をまたいだ直後にアップロードされた場合、
アップロード時刻基準だと実際の発生時刻と異なるhourパーティションに入ってしまい、
時間範囲でパーティションフィルタするクエリ（Grafana等）から漏れて見えるのを防ぐため。
バッチは短時間なので通常は1グループ（=1オブジェクト）、hour境界をまたぐ場合のみ複数になる。

保存先: s3://{BUCKET}/obd/year=YYYY/month=MM/day=DD/hour=HH/{device_id}-{uuid8}.ndjson
  （1行1JSON、改行区切り。Glueテーブルはsensor_dataと同じJsonSerDe+TextInputFormatで読める）
"""

import json
import os
import re
import uuid
from collections import defaultdict
from datetime import datetime, timezone

import boto3

s3 = boto3.client("s3")
BUCKET = os.environ["S3_BUCKET"]

MAX_READINGS_PER_BATCH = 500  # モバイル側上限100件に対する安全マージン
DEVICE_ID_RE = re.compile(r"^[A-Za-z0-9_-]{1,64}$")  # S3キーインジェクション対策

# ObdReading（Dart, camelCase）→ Glueカラム（snake_case）
_FIELD_MAP = {
    "rpm": "rpm", "speedKmh": "speed_kmh", "loadPct": "load_pct",
    "mapKpa": "map_kpa", "baroKpa": "baro_kpa", "boostKpa": "boost_kpa",
    "throttlePct": "throttle_pct", "timingDeg": "timing_deg",
    "ecuVoltage": "ecu_voltage", "mafGs": "maf_gs", "coolantC": "coolant_c",
    "fuelRateLph": "fuel_rate_lph", "stftPct": "stft_pct", "ltftPct": "ltft_pct",
    "o2B1s2V": "o2_b1s2_v", "o2B1s2TrimPct": "o2_b1s2_trim_pct",
    "engineRunTimeSec": "engine_run_time_sec", "milDistanceKm": "mil_distance_km",
    "o2S1Ratio": "o2_s1_ratio", "o2S1Voltage": "o2_s1_voltage",
    "evapPurgePct": "evap_purge_pct", "warmupsSinceCleared": "warmups_since_cleared",
    "distanceSinceClearedKm": "distance_since_cleared_km",
    "catalystTempC": "catalyst_temp_c", "absoluteLoadPct": "absolute_load_pct",
    "commandedAfr": "commanded_afr", "throttleBPct": "throttle_b_pct",
    "accelPedalDPct": "accel_pedal_d_pct", "accelPedalEPct": "accel_pedal_e_pct",
    "fuelType": "fuel_type", "secO2TrimStPct": "sec_o2_trim_st_pct",
    "secO2TrimLtPct": "sec_o2_trim_lt_pct", "valid": "valid",
}


def _sub(event: dict) -> str:
    return (
        event.get("requestContext", {})
        .get("authorizer", {})
        .get("jwt", {})
        .get("claims", {})
        .get("sub", "")
    )


def _error(status: int, msg: str) -> dict:
    return {
        "statusCode": status,
        "headers": {"Content-Type": "application/json"},
        "body": json.dumps({"error": msg}),
    }


def handler(event, context):
    sub = _sub(event)
    if not sub:
        return _error(401, "Unauthorized")

    try:
        body = json.loads(event.get("body") or "{}")
    except json.JSONDecodeError:
        return _error(400, "Invalid JSON")

    device_id = body.get("device_id", "")
    if not DEVICE_ID_RE.match(device_id):
        return _error(400, "Invalid device_id")

    readings = body.get("readings")
    if not isinstance(readings, list) or not readings:
        return _error(400, "readings must be a non-empty array")
    if len(readings) > MAX_READINGS_PER_BATCH:
        return _error(400, f"too many readings (max {MAX_READINGS_PER_BATCH})")

    ingest_ts = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")

    # readingごとのtsからパーティション（year/month/day/hour）を算出し、hourキーでグループ化
    groups = defaultdict(list)
    for i, r in enumerate(readings):
        if not isinstance(r, dict) or "ts" not in r:
            return _error(400, f"readings[{i}] missing ts")
        try:
            dt = datetime.fromtimestamp(int(r["ts"]), tz=timezone.utc)
        except (ValueError, OSError, TypeError):
            return _error(400, f"readings[{i}] invalid ts")

        record = {
            "device_id": device_id,
            "uploaded_by": sub,
            "ingest_ts": ingest_ts,
            "obd_ts": int(r["ts"]),
        }
        for src_key, glue_key in _FIELD_MAP.items():
            if src_key in r:
                record[glue_key] = r[src_key]

        partition = (dt.strftime("%Y"), dt.strftime("%m"), dt.strftime("%d"), dt.strftime("%H"))
        groups[partition].append(json.dumps(record))

    written = 0
    try:
        for (year, month, day, hour), lines in groups.items():
            key = (
                f"obd/year={year}/month={month}/day={day}/hour={hour}/"
                f"{device_id}-{uuid.uuid4().hex[:8]}.ndjson"
            )
            s3.put_object(
                Bucket=BUCKET,
                Key=key,
                Body="\n".join(lines),
                ContentType="application/x-ndjson",
            )
            print(f"[OK] s3://{BUCKET}/{key} readings={len(lines)}")
            written += len(lines)
    except Exception as e:
        print(f"[ERROR] {e}")
        return _error(500, "internal error")

    return {
        "statusCode": 200,
        "headers": {"Content-Type": "application/json"},
        "body": json.dumps({"ok": True, "count": written}),
    }
