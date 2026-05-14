"""
Admin API — admin グループユーザー専用

GET  /admin/devices                    全 esp32-gw-* デバイス一覧 + Shadow + Thing Groups
PUT  /admin/shadow/{device_id}         desired 部分更新
POST /admin/command/{device_id}        IoT Job 作成（ah_reset / charge_main_batt / upload_log）
PUT  /admin/groups/{device_id}         Thing Group メンバーシップ更新
"""

import json
import os
import time

import boto3

IOT_ENDPOINT = os.environ["IOT_ENDPOINT"]
ACCOUNT_ID   = os.environ["ACCOUNT_ID"]
REGION       = os.environ.get("AWS_REGION", "ap-northeast-1")
THING_PREFIX = "esp32-gw-"

iot_data = boto3.client("iot-data", endpoint_url=IOT_ENDPOINT)
iot      = boto3.client("iot")


def _resp(status, body):
    return {
        "statusCode": status,
        "headers": {"Content-Type": "application/json"},
        "body": json.dumps(body, ensure_ascii=False),
    }


def _err(status, msg):
    return _resp(status, {"error": msg})


def _is_admin(event):
    try:
        groups = event["requestContext"]["authorizer"]["jwt"]["claims"].get("cognito:groups", "")
        return "admin" in groups
    except Exception:
        return False


def handle_devices():
    # esp32-gw-* の Thing を全列挙
    things = []
    paginator = iot.get_paginator("list_things")
    for page in paginator.paginate():
        for t in page["things"]:
            if t["thingName"].startswith(THING_PREFIX):
                things.append(t["thingName"])

    # 利用可能な全 Thing Groups
    all_groups = [g["groupName"] for g in iot.list_thing_groups().get("thingGroups", [])]

    # 各 Thing の Shadow + Groups を取得
    result = []
    for name in things:
        entry = {"device_id": name, "shadow": {}, "groups": [], "last_seen_s": 0}
        try:
            resp = iot_data.get_thing_shadow(thingName=name)
            doc  = json.loads(resp["payload"].read())
            entry["shadow"]      = doc.get("state", {}).get("reported", {})
            entry["last_seen_s"] = doc.get("timestamp", 0)
        except Exception:
            pass
        try:
            entry["groups"] = [
                g["groupName"]
                for g in iot.list_thing_groups_for_thing(thingName=name).get("thingGroups", [])
            ]
        except Exception:
            pass
        result.append(entry)

    return _resp(200, {"devices": result, "all_groups": all_groups})


def handle_shadow(device_id, body):
    if not body:
        return _err(400, "body required")
    payload = json.dumps({"state": {"desired": body}})
    iot_data.update_thing_shadow(thingName=device_id, payload=payload.encode())
    return _resp(200, {"ok": True})


def handle_command(device_id, body):
    operation = body.get("operation")
    if operation not in ("ah_reset", "charge_main_batt"):
        return _err(400, f"unknown operation: {operation}")

    doc = {"operation": operation}
    if operation == "charge_main_batt":
        doc["timeout_sec"] = int(body.get("timeout_sec", 1200))

    job_id    = f"cmd-{device_id}-{int(time.time())}"
    thing_arn = f"arn:aws:iot:{REGION}:{ACCOUNT_ID}:thing/{device_id}"
    iot.create_job(
        jobId=job_id,
        targets=[thing_arn],
        document=json.dumps(doc),
        targetSelection="SNAPSHOT",
        timeoutConfig={"inProgressTimeoutInMinutes": 60},
    )
    return _resp(200, {"job_id": job_id})


def handle_groups(device_id, body):
    desired = set(body.get("groups", []))
    current = {
        g["groupName"]
        for g in iot.list_thing_groups_for_thing(thingName=device_id).get("thingGroups", [])
    }
    for g in desired - current:
        iot.add_thing_to_thing_group(thingName=device_id, thingGroupName=g)
    for g in current - desired:
        iot.remove_thing_from_thing_group(thingName=device_id, thingGroupName=g)
    return _resp(200, {"groups": sorted(desired)})


def handler(event, context):
    if not _is_admin(event):
        return _err(403, "admin only")

    proxy  = (event.get("pathParameters") or {}).get("proxy", "")
    method = event["requestContext"]["http"]["method"]
    segs   = proxy.split("/")

    try:
        if segs[0] == "devices" and method == "GET":
            return handle_devices()
        elif segs[0] == "shadow" and len(segs) == 2 and method == "PUT":
            return handle_shadow(segs[1], json.loads(event.get("body") or "{}"))
        elif segs[0] == "command" and len(segs) == 2 and method == "POST":
            return handle_command(segs[1], json.loads(event.get("body") or "{}"))
        elif segs[0] == "groups" and len(segs) == 2 and method == "PUT":
            return handle_groups(segs[1], json.loads(event.get("body") or "{}"))
        return _err(404, "not found")
    except Exception as e:
        return _err(500, str(e))
