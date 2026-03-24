"""
API Gateway → Lambda → Athena → S3

非同期ポーリング方式:
  1. GET /data?start_time=...&end_time=...  → Athena クエリ投入、{execution_id, status:"RUNNING"} を即返す
  2. GET /data?execution_id=xxx             → クエリ状態確認。SUCCEEDED 時はデータを返す
  3. GET /data?mode=range                   → S3 パーティションから最古・最新タイムスタンプを返す（同期）

クエリパラメータ（クエリ投入時）:
  start_time : ISO8601 UTC（例: 2026-01-01T00:00:00Z）
  end_time   : ISO8601 UTC（例: 2026-01-02T00:00:00Z）
  hours      : 後方互換。過去何時間分（デフォルト 24）。start_time/end_time が指定された場合は無視
  type       : "battery" or "thermometer" or "co2meter"（省略時はすべて）
  device_id  : デバイスIDでフィルタ（省略可）
  addr       : SwitchBot MAC アドレスでフィルタ（省略可）
"""

import json
import os
import re
from datetime import datetime, timezone, timedelta

import boto3
from botocore.exceptions import ClientError

athena    = boto3.client("athena")
s3        = boto3.client("s3")
DATABASE  = os.environ["ATHENA_DATABASE"]
WORKGROUP = os.environ["ATHENA_WORKGROUP"]
S3_BUCKET = os.environ["S3_BUCKET"]


def _partition_filters_range(start_dt: datetime, end_dt: datetime) -> list[str]:
    """start_dt〜end_dt をカバーする最小限のパーティションフィルタを返す。"""
    cur = start_dt.replace(minute=0, second=0, microsecond=0)

    years, months, days, hrs = set(), set(), set(), set()
    while cur <= end_dt:
        years.add(str(cur.year))
        months.add(cur.strftime('%m'))
        days.add(cur.strftime('%d'))
        hrs.add(cur.strftime('%H'))
        cur += timedelta(hours=1)

    def _in(col, vals):
        v = sorted(vals)
        return f"{col} = '{v[0]}'" if len(v) == 1 else f"{col} IN ({', '.join(repr(x) for x in v)})"

    filters = [_in('year', years), _in('month', months), _in('day', days)]
    total_hours = (end_dt - start_dt).total_seconds() / 3600
    if total_hours <= 72:
        filters.append(_in('hour', hrs))
    return filters


_VALID_ADDR      = re.compile(r'^[0-9A-Fa-f]{2}(:[0-9A-Fa-f]{2}){5}$')
_VALID_DEVICE_ID = re.compile(r'^[\w\-]{1,64}$')
_VALID_TYPES     = {"battery", "thermometer", "co2meter"}
_ISO8601         = re.compile(r'^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z$')

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


def _parse_iso(s: str) -> datetime:
    return datetime.strptime(s, "%Y-%m-%dT%H:%M:%SZ").replace(tzinfo=timezone.utc)


def _validate_inputs(sensor_type, device_id, start_dt: datetime, end_dt: datetime, addr):
    if sensor_type and sensor_type not in _VALID_TYPES:
        raise ValueError(f"Invalid type: {sensor_type!r}")
    if device_id and not _VALID_DEVICE_ID.match(device_id):
        raise ValueError("Invalid device_id format")
    if addr and not _VALID_ADDR.match(addr):
        raise ValueError("Invalid addr format")
    if end_dt <= start_dt:
        raise ValueError("end_time must be after start_time")
    span_hours = (end_dt - start_dt).total_seconds() / 3600
    if span_hours > 720:
        raise ValueError("Time range must not exceed 720 hours (30 days)")


def _select_list(sensor_type: str) -> str:
    """センサタイプの SELECT 列リストを返す（欠けるカラムは NULL 埋め）。"""
    own = _TYPE_OWN_COLS[sensor_type]
    cols = ["ts", "type", "device_id"]
    for col in _ALL_EXTRA_COLS:
        cols.append(col if col in own else f"NULL AS {col}")
    cols.append('"$path" AS s3_key')
    return ", ".join(cols)


def _build_query(sensor_type: str | None, device_id: str | None,
                 start_dt: datetime, end_dt: datetime, addr: str | None) -> str:
    start_iso = start_dt.strftime("%Y-%m-%dT%H:%M:%SZ")
    end_iso   = end_dt.strftime("%Y-%m-%dT%H:%M:%SZ")
    base_filters = (
        _partition_filters_range(start_dt, end_dt)
        + [f"ts >= '{start_iso}'", f"ts <= '{end_iso}'"]
    )
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

    return "\nUNION ALL\n".join(subqueries) + "\nORDER BY ts ASC"


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

    return _downsample(rows)


_MAX_POINTS_PER_SERIES = 2000

def _downsample(rows: list) -> list:
    """シリーズ（device_id × type × addr/id）ごとに等間隔で間引く。
    ts ASC で取得済みなので時系列順が保証されている。"""
    from collections import defaultdict
    groups: dict[tuple, list] = defaultdict(list)
    for r in rows:
        key = (r.get('device_id'), r.get('type'), r.get('addr') or r.get('id'))
        groups[key].append(r)

    result = []
    for pts in groups.values():
        if len(pts) > _MAX_POINTS_PER_SERIES:
            step = len(pts) / _MAX_POINTS_PER_SERIES
            pts = [pts[int(i * step)] for i in range(_MAX_POINTS_PER_SERIES)]
        result.extend(pts)

    result.sort(key=lambda r: r.get('ts') or '', reverse=True)
    return result


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


def _list_partition_values(prefix: str, key_name: str) -> list[str]:
    """S3 Hive パーティション（key_name=value/）のバリュー一覧を返す。"""
    values = []
    kwargs = {"Bucket": S3_BUCKET, "Prefix": prefix, "Delimiter": "/"}
    while True:
        resp = s3.list_objects_v2(**kwargs)
        for cp in resp.get("CommonPrefixes", []):
            # e.g. "raw/year=2025/" → "2025"
            part = cp["Prefix"].rstrip("/").split("/")[-1]
            if "=" in part and part.split("=")[0] == key_name:
                values.append(part.split("=")[1])
        if resp.get("IsTruncated"):
            kwargs["ContinuationToken"] = resp["NextContinuationToken"]
        else:
            break
    return sorted(values)


def _get_data_range() -> dict:
    """S3 パーティションを走査して最古・最新タイムスタンプを返す（Athena 不使用）。"""
    try:
        raw_prefix = "raw/"
        years = _list_partition_values(raw_prefix, "year")
        if not years:
            return {"statusCode": 200, "headers": {"Content-Type": "application/json"},
                    "body": json.dumps({"min_ts": None, "max_ts": None})}

        def _earliest(year):
            months = _list_partition_values(f"{raw_prefix}year={year}/", "month")
            if not months:
                return None
            month = months[0]
            days = _list_partition_values(f"{raw_prefix}year={year}/month={month}/", "day")
            if not days:
                return None
            day = days[0]
            hrs = _list_partition_values(f"{raw_prefix}year={year}/month={month}/day={day}/", "hour")
            hour = hrs[0] if hrs else "00"
            return f"{year}-{month}-{day}T{hour}:00:00Z"

        def _latest(year):
            months = _list_partition_values(f"{raw_prefix}year={year}/", "month")
            if not months:
                return None
            month = months[-1]
            days = _list_partition_values(f"{raw_prefix}year={year}/month={month}/", "day")
            if not days:
                return None
            day = days[-1]
            hrs = _list_partition_values(f"{raw_prefix}year={year}/month={month}/day={day}/", "hour")
            hour = hrs[-1] if hrs else "23"
            return f"{year}-{month}-{day}T{hour}:59:59Z"

        min_ts = _earliest(years[0])
        max_ts = _latest(years[-1])
        # max_ts が未来にならないよう現在時刻でキャップ
        now_iso = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
        if max_ts and max_ts > now_iso:
            max_ts = now_iso

        return {
            "statusCode": 200,
            "headers": {"Content-Type": "application/json"},
            "body": json.dumps({"min_ts": min_ts, "max_ts": max_ts}),
        }
    except Exception as e:
        print(f"[ERROR] _get_data_range: {e}")
        return {
            "statusCode": 500,
            "headers": {"Content-Type": "application/json"},
            "body": json.dumps({"error": str(e)}),
        }


def handler(event, context):
    params = event.get("queryStringParameters") or {}
    sub = event.get("requestContext", {}).get("authorizer", {}).get("jwt", {}).get("claims", {}).get("sub", "")

    # mode=range: S3 パーティションから最古・最新を返す（同期、Athena 不使用）
    if params.get("mode") == "range":
        return _get_data_range()

    # 既存クエリの状態確認モード
    execution_id = params.get("execution_id")
    if execution_id:
        return _get_results(execution_id, sub)

    # 新規クエリ投入モード
    now = datetime.now(timezone.utc)

    # start_time / end_time が指定されていれば優先、なければ hours で計算
    raw_start = params.get("start_time")
    raw_end   = params.get("end_time")
    if raw_start or raw_end:
        if not raw_start or not raw_end:
            return {"statusCode": 400, "headers": {"Content-Type": "application/json"},
                    "body": json.dumps({"error": "Both start_time and end_time are required"})}
        if not _ISO8601.match(raw_start) or not _ISO8601.match(raw_end):
            return {"statusCode": 400, "headers": {"Content-Type": "application/json"},
                    "body": json.dumps({"error": "start_time/end_time must be ISO8601 UTC (YYYY-MM-DDTHH:MM:SSZ)"})}
        try:
            start_dt = _parse_iso(raw_start)
            end_dt   = _parse_iso(raw_end)
        except ValueError:
            return {"statusCode": 400, "headers": {"Content-Type": "application/json"},
                    "body": json.dumps({"error": "Invalid datetime format"})}
    else:
        try:
            hours = int(params.get("hours", 24))
        except ValueError:
            return {"statusCode": 400, "headers": {"Content-Type": "application/json"},
                    "body": json.dumps({"error": "hours must be an integer"})}
        start_dt = now - timedelta(hours=hours)
        end_dt   = now

    sensor_type = params.get("type")
    device_id   = params.get("device_id")
    addr        = params.get("addr")

    try:
        _validate_inputs(sensor_type, device_id, start_dt, end_dt, addr)
    except ValueError as e:
        return {"statusCode": 400, "headers": {"Content-Type": "application/json"},
                "body": json.dumps({"error": str(e)})}

    query = _build_query(sensor_type, device_id, start_dt, end_dt, addr)

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
