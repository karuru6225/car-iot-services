"""
DELETE /data?addr=AA:BB:CC:DD:EE:FF&hours=72
DELETE /data?id=voltage_1&hours=72

フロー:
  1. Athena で addr/id + 時間範囲 → 対象パーティション（year/month/day/hour）を取得
  2. 各パーティションを S3 ListObjectsV2 で列挙
  3. JSON を読んで addr/id が一致するオブジェクトを DeleteObject
  4. 削除件数を返す
"""

import json
import os
import re
import time
from datetime import datetime, timezone, timedelta

import boto3

s3 = boto3.client("s3")
athena = boto3.client("athena")

S3_BUCKET = os.environ["S3_BUCKET"]
DATABASE = os.environ["ATHENA_DATABASE"]
WORKGROUP = os.environ["ATHENA_WORKGROUP"]


def _ts_ago(hours):
    dt = datetime.now(timezone.utc) - timedelta(hours=hours)
    return dt.strftime("%Y-%m-%dT%H:%M:%SZ")


def _run_athena(query):
    resp = athena.start_query_execution(
        QueryString=query,
        QueryExecutionContext={"Database": DATABASE},
        WorkGroup=WORKGROUP,
    )
    eid = resp["QueryExecutionId"]
    for _ in range(30):
        status = athena.get_query_execution(QueryExecutionId=eid)
        state = status["QueryExecution"]["Status"]["State"]
        if state == "SUCCEEDED":
            return eid
        if state in ("FAILED", "CANCELLED"):
            reason = status["QueryExecution"]["Status"].get("StateChangeReason", "")
            raise RuntimeError(f"Athena {state}: {reason}")
        time.sleep(1)
    athena.stop_query_execution(QueryExecutionId=eid)
    raise TimeoutError("Athena timed out")


def _partition_filters(hours):
    """hours 範囲のパーティションフィルタ（year/month/day/hour の IN 句）を返す。"""
    now   = datetime.now(timezone.utc)
    start = now - timedelta(hours=hours)
    cur   = start.replace(minute=0, second=0, microsecond=0)

    years, months, days, hrs = set(), set(), set(), set()
    while cur <= now:
        years.add(str(cur.year))
        months.add(cur.strftime('%m'))
        days.add(cur.strftime('%d'))
        hrs.add(cur.strftime('%H'))
        cur += timedelta(hours=1)

    def _in(col, vals):
        v = sorted(vals)
        return f"{col} = '{v[0]}'" if len(v) == 1 else f"{col} IN ({', '.join(repr(x) for x in v)})"

    filters = [_in('year', years), _in('month', months), _in('day', days)]
    if hours <= 72:
        filters.append(_in('hour', hrs))
    return filters


def _get_partitions(addr, sensor_id, hours):
    """削除対象のパーティション（year/month/day/hour）の一覧を Athena で取得する。"""
    part_filters = _partition_filters(hours)
    ts_filter = f"ts >= '{_ts_ago(hours)}'"
    if addr:
        cond = f"addr = '{addr}'"
    else:
        cond = f"id = '{sensor_id}'"

    query = f"""
        SELECT DISTINCT year, month, day, hour
        FROM sensor_data
        WHERE {cond} AND {' AND '.join(part_filters)} AND {ts_filter}
    """
    eid = _run_athena(query)
    result = athena.get_query_results(QueryExecutionId=eid)
    partitions = []
    for row in result["ResultSet"]["Rows"][1:]:  # 先頭行はヘッダー
        vals = [d.get("VarCharValue", "") for d in row["Data"]]
        if len(vals) == 4:
            partitions.append(tuple(vals))
    return partitions


def _delete_from_partition(addr, sensor_id, year, month, day, hour):
    """パーティション内のオブジェクトを読んで addr/id が一致するものを削除する。"""
    prefix = f"raw/year={year}/month={month}/day={day}/hour={hour}/"
    deleted = 0
    paginator = s3.get_paginator("list_objects_v2")
    for page in paginator.paginate(Bucket=S3_BUCKET, Prefix=prefix):
        for obj in page.get("Contents", []):
            key = obj["Key"]
            body = s3.get_object(Bucket=S3_BUCKET, Key=key)["Body"].read()
            try:
                data = json.loads(body)
            except Exception:
                continue
            match = (addr and data.get("addr") == addr) or (
                sensor_id and data.get("id") == sensor_id
            )
            if match:
                s3.delete_object(Bucket=S3_BUCKET, Key=key)
                deleted += 1
    return deleted


def _is_admin(event: dict) -> bool:
    """cognito:groups クレームに admin が含まれているか確認する。"""
    try:
        groups = event["requestContext"]["authorizer"]["jwt"]["claims"].get("cognito:groups", "")
        return "admin" in groups
    except (KeyError, TypeError):
        return False


_VALID_S3_KEY = re.compile(
    r'^raw/year=\d{4}/month=\d{2}/day=\d{2}/hour=\d{2}/[\w\-\.]+\.json$'
)


def _delete_by_keys(s3_keys: list) -> dict:
    """S3 キーのリストを直接削除する。Athena の $path 形式（s3://bucket/key）に対応。"""
    prefix = f"s3://{S3_BUCKET}/"
    deleted = 0
    for key in s3_keys:
        if key.startswith(prefix):
            key = key[len(prefix):]
        if not _VALID_S3_KEY.match(key):
            print(f"[WARN] rejected invalid s3 key: {key}")
            continue
        try:
            s3.delete_object(Bucket=S3_BUCKET, Key=key)
            deleted += 1
        except Exception as e:
            print(f"[WARN] delete failed for {key}: {e}")
    return {
        "statusCode": 200,
        "headers": {"Content-Type": "application/json"},
        "body": json.dumps({"deleted": deleted}),
    }


def handler(event, context):
    # 権限チェック: admin グループのみ削除可
    if not _is_admin(event):
        return {
            "statusCode": 403,
            "headers": {"Content-Type": "application/json"},
            "body": json.dumps({"error": "削除権限がありません（admin グループが必要です）"}),
        }

    # s3_keys による直接削除（クエリ結果の $path を渡す方式）
    body = {}
    if event.get("body"):
        try:
            body = json.loads(event["body"])
        except Exception:
            pass
    s3_keys = body.get("s3_keys")
    if s3_keys:
        return _delete_by_keys(s3_keys)

    # 従来の addr/id + hours による削除
    params = event.get("queryStringParameters") or {}
    addr = params.get("addr")
    sensor_id = params.get("id")
    hours = int(params.get("hours", 72))

    if not addr and not sensor_id:
        return {
            "statusCode": 400,
            "headers": {"Content-Type": "application/json"},
            "body": json.dumps({"error": "addr または id が必要です"}),
        }

    try:
        partitions = _get_partitions(addr, sensor_id, hours)
        deleted = 0
        for year, month, day, hour in partitions:
            deleted += _delete_from_partition(addr, sensor_id, year, month, day, hour)
    except Exception as e:
        print(f"[ERROR] {e}")
        return {
            "statusCode": 500,
            "headers": {"Content-Type": "application/json"},
            "body": json.dumps({"error": str(e)}),
        }

    return {
        "statusCode": 200,
        "headers": {"Content-Type": "application/json"},
        "body": json.dumps({"deleted": deleted}),
    }
