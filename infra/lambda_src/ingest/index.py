"""
IoT Core Topic Rule → Lambda → S3

受け取るペイロード例:
  電圧:    {"device_id":"m5stamp-lite-01", "voltage":12.34, "id":"voltage_1"}
  温湿度:  {"device_id":"m5stamp-lite-01", "addr":"AA:BB:CC:DD:EE:FF",
             "temp":25.0, "humidity":60, "battery":80, "rssi":-70}

保存先:
  s3://{BUCKET}/raw/year=YYYY/month=MM/day=DD/hour=HH/{device_id}-{uuid8}.json
"""

import json
import os
import uuid
from datetime import datetime, timezone

import boto3

s3 = boto3.client("s3")
BUCKET = os.environ["S3_BUCKET"]


def handler(event, context):
    device_id = event.get("device_id", "unknown")

    ts_str = event.get("ts", "")
    if ts_str:
        try:
            dt = datetime.strptime(ts_str, "%Y-%m-%dT%H:%M:%SZ").replace(tzinfo=timezone.utc)
        except ValueError:
            dt = datetime.now(timezone.utc)
    else:
        dt = datetime.now(timezone.utc)

    if "type" not in event:
        print(f"[SKIP] 認識できないペイロード: {json.dumps(event)}")
        return

    key = (
        f"raw/"
        f"year={dt.strftime('%Y')}/"
        f"month={dt.strftime('%m')}/"
        f"day={dt.strftime('%d')}/"
        f"hour={dt.strftime('%H')}/"
        f"{device_id}-{uuid.uuid4().hex[:8]}.json"
    )

    payload = {
        **event,
        "device_id": device_id,
        "ts": dt.strftime("%Y-%m-%dT%H:%M:%SZ"),
    }

    try:
        s3.put_object(
            Bucket=BUCKET,
            Key=key,
            Body=json.dumps(payload),
            ContentType="application/json",
        )
        print(f"[OK] s3://{BUCKET}/{key}")
    except Exception as e:
        print(f"[ERROR] {e}")
        raise
