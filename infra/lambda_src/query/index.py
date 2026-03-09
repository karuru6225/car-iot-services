"""
API Gateway → Lambda → Athena → S3

非同期ポーリング方式:
  1. GET /data?hours=24&type=voltage  → Athena クエリ投入、{execution_id, status:"RUNNING"} を即返す
  2. GET /data?execution_id=xxx       → クエリ状態確認。SUCCEEDED 時はデータを返す

クエリパラメータ（クエリ投入時）:
  hours     : 過去何時間分（デフォルト 24）
  type      : "battery" or "thermometer" or "co2meter"（省略時はすべて）
  device_id : デバイスIDでフィルタ（省略可）
  addr      : SwitchBot MAC アドレスでフィルタ（省略可）
"""

import json
import os
import re

import boto3
from botocore.exceptions import ClientError

athena    = boto3.client("athena")
s3        = boto3.client("s3")
DATABASE  = os.environ["ATHENA_DATABASE"]
WORKGROUP = os.environ["ATHENA_WORKGROUP"]
S3_BUCKET = os.environ["S3_BUCKET"]


def _partition_filters(hours):
    """hours 範囲をカバーする最小限のパーティションフィルタを返す。
    year >= '2026' だと projection が全年月日時をスキャンして遅くなるため、
    実際に存在しうるパーティションだけを IN 句で指定する。"""
    from datetime import datetime, timezone, timedelta
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
    # hour は1週間超だと多くなりすぎるので省略
    if hours <= 72:
        filters.append(_in('hour', hrs))
    return filters


_VALID_ADDR      = re.compile(r'^[0-9A-Fa-f]{2}(:[0-9A-Fa-f]{2}){5}$')
_VALID_DEVICE_ID = re.compile(r'^[\w\-]{1,64}$')
_VALID_TYPES     = {"battery", "thermometer", "co2meter"}

# 全カラムの順序（UNION ALL の列順を統一するために使う）
_ALL_EXTRA_COLS = ["id", "voltage", "addr", "temp", "humidity", "battery", "rssi", "co2"]

# センサタイプごとの実カラム集合
_TYPE_OWN_COLS: dict[str, set[str]] = {
    "battery":     {"id", "voltage"},
    "thermometer": {"addr", "temp", "humidity", "battery", "rssi"},
    "co2meter":    {"addr", "temp", "humidity", "battery", "rssi", "co2"},
}

# addr フィルタを受け付けるタイプ
_ADDR_FILTER_TYPES = {"thermometer", "co2meter"}


def _get_labels(sub: str) -> dict:
    """ユーザーのラベル設定を S3 から取得する。未設定時は空 dict を返す。"""
    try:
        obj = s3.get_object(Bucket=S3_BUCKET, Key=f"labels/{sub}.json")
        return json.loads(obj["Body"].read())
    except ClientError as e:
        if e.response["Error"]["Code"] == "NoSuchKey":
            return {}
        print(f"[WARN] labels fetch failed: {e}")
        return {}
    except Exception as e:
        print(f"[WARN] labels fetch failed: {e}")
        return {}


def _validate_inputs(sensor_type, device_id, hours, addr):
    if sensor_type and sensor_type not in _VALID_TYPES:
        raise ValueError(f"Invalid type: {sensor_type!r}")
    if device_id and not _VALID_DEVICE_ID.match(device_id):
        raise ValueError("Invalid device_id format")
    if addr and not _VALID_ADDR.match(addr):
        raise ValueError("Invalid addr format")
    if not (1 <= hours <= 720):
        raise ValueError("hours must be between 1 and 720")


def _select_list(sensor_type: str) -> str:
    """センサタイプの SELECT 列リストを返す（欠けるカラムは NULL 埋め）。"""
    own = _TYPE_OWN_COLS[sensor_type]
    cols = ["ts", "type", "device_id"]
    for col in _ALL_EXTRA_COLS:
        cols.append(col if col in own else f"NULL AS {col}")
    cols.append('"$path" AS s3_key')
    return ", ".join(cols)


def _build_query(sensor_type: str | None, device_id: str | None, hours: int, addr: str | None) -> str:
    base_filters = _partition_filters(hours) + [f"ts >= '{_hours_ago_iso(hours)}'"]
    if device_id:
        base_filters.append(f"device_id = '{device_id}'")

    types = [sensor_type] if sensor_type else list(_TYPE_OWN_COLS)
    subqueries = []
    for t in types:
        filters = list(base_filters)
        if addr and t in _ADDR_FILTER_TYPES:
            filters.append(f"addr = '{addr}'")
        where = " AND ".join(filters + [f"type = '{t}'"])
        subqueries.append(f"SELECT {_select_list(t)} FROM sensor_data WHERE {where}")

    return "\nUNION ALL\n".join(subqueries) + "\nORDER BY ts DESC\nLIMIT 7000"


def _hours_ago_iso(hours):
    from datetime import datetime, timezone, timedelta
    dt = datetime.now(timezone.utc) - timedelta(hours=hours)
    return dt.strftime("%Y-%m-%dT%H:%M:%SZ")


def _parse_results(execution_id):
    """クエリ結果を dict のリストに変換する。数値は float に変換する。"""
    rows = []
    col_names = None
    kwargs = {"QueryExecutionId": execution_id}

    while True:
        result = athena.get_query_results(**kwargs)
        result_rows = result["ResultSet"]["Rows"]

        # 初回ページのみ列名取得＆ヘッダー行スキップ
        if col_names is None:
            col_names = [col["Label"] for col in result["ResultSet"]["ResultSetMetadata"]["ColumnInfo"]]
            result_rows = result_rows[1:]

        for row in result_rows:
            values = [d.get("VarCharValue") for d in row["Data"]]
            obj = {}
            for col, val in zip(col_names, values):
                if val is None:
                    obj[col] = None
                    continue
                try:
                    obj[col] = float(val)
                except (ValueError, TypeError):
                    obj[col] = val
            rows.append(obj)

        next_token = result.get("NextToken")
        if not next_token:
            break
        kwargs["NextToken"] = next_token

    return rows


def _get_results(execution_id: str, sub: str) -> dict:
    """既存クエリの状態確認 / 結果取得。"""
    try:
        status = athena.get_query_execution(QueryExecutionId=execution_id)
        state = status["QueryExecution"]["Status"]["State"]

        if state in ("RUNNING", "QUEUED"):
            return {
                "statusCode": 200,
                "headers": {"Content-Type": "application/json"},
                "body": json.dumps({"execution_id": execution_id, "status": state}),
            }

        if state in ("FAILED", "CANCELLED"):
            reason = status["QueryExecution"]["Status"].get("StateChangeReason", "")
            return {
                "statusCode": 200,
                "headers": {"Content-Type": "application/json"},
                "body": json.dumps({"execution_id": execution_id, "status": state, "error": reason}),
            }

        # SUCCEEDED
        rows = _parse_results(execution_id)
        labels = _get_labels(sub)
        return {
            "statusCode": 200,
            "headers": {"Content-Type": "application/json"},
            "body": json.dumps({"execution_id": execution_id, "status": "SUCCEEDED", "data": rows, "labels": labels}),
        }
    except Exception as e:
        print(f"[ERROR] {e}")
        return {
            "statusCode": 500,
            "headers": {"Content-Type": "application/json"},
            "body": json.dumps({"error": str(e)}),
        }


def handler(event, context):
    params = event.get("queryStringParameters") or {}
    sub = event.get("requestContext", {}).get("authorizer", {}).get("jwt", {}).get("claims", {}).get("sub", "")

    # 既存クエリの状態確認モード
    execution_id = params.get("execution_id")
    if execution_id:
        return _get_results(execution_id, sub)

    # 新規クエリ投入モード
    try:
        hours = int(params.get("hours", 24))
    except ValueError:
        return {
            "statusCode": 400,
            "headers": {"Content-Type": "application/json"},
            "body": json.dumps({"error": "hours must be an integer"}),
        }
    sensor_type = params.get("type")
    device_id = params.get("device_id")
    addr = params.get("addr")

    try:
        _validate_inputs(sensor_type, device_id, hours, addr)
    except ValueError as e:
        return {
            "statusCode": 400,
            "headers": {"Content-Type": "application/json"},
            "body": json.dumps({"error": str(e)}),
        }

    query = _build_query(sensor_type, device_id, hours, addr)

    try:
        resp = athena.start_query_execution(
            QueryString=query,
            QueryExecutionContext={"Database": DATABASE},
            WorkGroup=WORKGROUP,
        )
        eid = resp["QueryExecutionId"]
        return {
            "statusCode": 200,
            "headers": {"Content-Type": "application/json"},
            "body": json.dumps({"execution_id": eid, "status": "RUNNING"}),
        }
    except Exception as e:
        print(f"[ERROR] {e}")
        return {
            "statusCode": 500,
            "headers": {"Content-Type": "application/json"},
            "body": json.dumps({"error": str(e)}),
        }
