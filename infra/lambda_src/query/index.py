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

athena = boto3.client("athena")
DATABASE = os.environ["ATHENA_DATABASE"]
WORKGROUP = os.environ["ATHENA_WORKGROUP"]


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


def _validate_inputs(sensor_type, device_id, hours, addr):
    if sensor_type and sensor_type not in _VALID_TYPES:
        raise ValueError(f"Invalid type: {sensor_type!r}")
    if device_id and not _VALID_DEVICE_ID.match(device_id):
        raise ValueError("Invalid device_id format")
    if addr and not _VALID_ADDR.match(addr):
        raise ValueError("Invalid addr format")
    if not (1 <= hours <= 720):
        raise ValueError("hours must be between 1 and 720")


def _build_query(sensor_type, device_id, hours, addr):
    base_filters = _partition_filters(hours) + [f"ts >= '{_hours_ago_iso(hours)}'"]
    if device_id:
        base_filters.append(f"device_id = '{device_id}'")

    if sensor_type == "battery":
        where = " AND ".join(base_filters + ["type = 'battery'"])
        return f"""
            SELECT ts, type, device_id, id, voltage, "$path" AS s3_key
            FROM sensor_data
            WHERE {where}
            ORDER BY ts DESC
            LIMIT 2000
        """

    if sensor_type == "thermometer":
        if addr:
            base_filters.append(f"addr = '{addr}'")
        where = " AND ".join(base_filters + ["type = 'thermometer'"])
        return f"""
            SELECT ts, type, device_id, addr, temp, humidity, battery, rssi, NULL AS co2, "$path" AS s3_key
            FROM sensor_data
            WHERE {where}
            ORDER BY ts DESC
            LIMIT 2000
        """

    if sensor_type == "co2meter":
        if addr:
            base_filters.append(f"addr = '{addr}'")
        where = " AND ".join(base_filters + ["type = 'co2meter'"])
        return f"""
            SELECT ts, type, device_id, addr, temp, humidity, battery, rssi, co2, "$path" AS s3_key
            FROM sensor_data
            WHERE {where}
            ORDER BY ts DESC
            LIMIT 2000
        """

    # type 未指定: 電圧・温湿度・CO2 を UNION
    where = " AND ".join(base_filters)
    return f"""
        SELECT ts, type, device_id,
               id, voltage, NULL AS addr, NULL AS temp, NULL AS humidity, NULL AS battery, NULL AS rssi, NULL AS co2, "$path" AS s3_key
        FROM sensor_data
        WHERE {where} AND type = 'battery'
        UNION ALL
        SELECT ts, type, device_id,
               NULL AS id, NULL AS voltage, addr, temp, humidity, battery, rssi, NULL AS co2, "$path" AS s3_key
        FROM sensor_data
        WHERE {where} AND type = 'thermometer'
        UNION ALL
        SELECT ts, type, device_id,
               NULL AS id, NULL AS voltage, addr, temp, humidity, battery, rssi, co2, "$path" AS s3_key
        FROM sensor_data
        WHERE {where} AND type = 'co2meter'
        ORDER BY ts DESC
        LIMIT 2000
    """


def _hours_ago_iso(hours):
    from datetime import datetime, timezone, timedelta
    dt = datetime.now(timezone.utc) - timedelta(hours=hours)
    return dt.strftime("%Y-%m-%dT%H:%M:%SZ")


def _parse_results(execution_id):
    """クエリ結果を dict のリストに変換する。数値は float に変換する。"""
    result = athena.get_query_results(QueryExecutionId=execution_id)
    col_names = [col["Label"] for col in result["ResultSet"]["ResultSetMetadata"]["ColumnInfo"]]
    rows = []
    for row in result["ResultSet"]["Rows"][1:]:  # 先頭行はヘッダー
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
    return rows


def _get_results(execution_id):
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
        return {
            "statusCode": 200,
            "headers": {"Content-Type": "application/json"},
            "body": json.dumps({"execution_id": execution_id, "status": "SUCCEEDED", "data": rows}),
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

    # 既存クエリの状態確認モード
    execution_id = params.get("execution_id")
    if execution_id:
        return _get_results(execution_id)

    # 新規クエリ投入モード
    hours = int(params.get("hours", 24))
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
