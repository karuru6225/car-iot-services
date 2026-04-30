import boto3
import json
import os
from datetime import datetime, timezone

iot = boto3.client("iot-data", endpoint_url=os.environ["IOT_ENDPOINT"])
THING_NAME = os.environ["THING_NAME"]

ALIVE_THRESHOLD_S = 600  # 10分以内なら生存とみなす


def handler(event, context):
    try:
        resp = iot.get_thing_shadow(thingName=THING_NAME)
        shadow = json.loads(resp["payload"].read())
    except iot.exceptions.ResourceNotFoundException:
        return {
            "statusCode": 200,
            "headers": {"Content-Type": "application/json"},
            "body": json.dumps({"device_id": THING_NAME, "alive": False, "error": "shadow not found"}),
        }

    reported = shadow.get("state", {}).get("reported", {})
    last_seen_s = shadow.get("timestamp", 0)  # Shadow 最終更新のサーバー側タイムスタンプ

    now_s = datetime.now(timezone.utc).timestamp()
    age_s = int(now_s - last_seen_s)

    return {
        "statusCode": 200,
        "headers": {"Content-Type": "application/json"},
        "body": json.dumps({
            "device_id": THING_NAME,
            "last_seen_s": last_seen_s,
            "age_s": age_s,
            "alive": age_s < ALIVE_THRESHOLD_S,
            "sub_v": float(reported.get("sub_v", 0)),
            "main_v": float(reported.get("main_v", 0)),
        }),
    }
