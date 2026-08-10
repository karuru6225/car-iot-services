"""
EventBridge Scheduler（1分ごと） → trip_sweep Lambda

device-watermarkテーブルのopen-index GSI（open_marker="OPEN"の未処理セッションのみ
載るスパースインデックス、infra/trip_analysis.tf参照）をQueryし、次のいずれかで
トリップ終了を判定する:
  (a) valid=falseへ転じてから5秒（INVALID_TAIL_SEC）
      モバイルアプリ側はvalid=falseへの遷移直後10秒だけ尾流しアップロード＋即flush
      する（mobile/lib/services/obd_uploader.dart）ため、この5秒は十分な余裕を持つ
  (b) 直近データから10分無音（GAP_TIMEOUT_SEC）
      旧アプリ・アップロード失敗時のフォールバック

終了と判定したセッションはobd_dataをAthenaで集計し、trip-summaryテーブルへ保存
してからwatermark側のopen_marker/session_start/invalid_sinceを削除する（GSIから
外れ、次回スイープの対象から外れる）。Athena集計に失敗した場合はwatermarkを
クローズせず、次回スイープでリトライする。
"""

import os
import time
from datetime import datetime, timedelta, timezone
from decimal import Decimal

import boto3

dynamodb = boto3.resource("dynamodb")
athena = boto3.client("athena")

WATERMARK_TABLE = os.environ["WATERMARK_TABLE"]
TRIP_SUMMARY_TABLE = os.environ["TRIP_SUMMARY_TABLE"]
ATHENA_DATABASE = os.environ["ATHENA_DATABASE"]
ATHENA_WORKGROUP = os.environ["ATHENA_WORKGROUP"]

watermark_table = dynamodb.Table(WATERMARK_TABLE)
trip_summary_table = dynamodb.Table(TRIP_SUMMARY_TABLE)

INVALID_TAIL_SEC = 5
GAP_TIMEOUT_SEC = 600
ATHENA_POLL_INTERVAL_SEC = 1.0
ATHENA_POLL_TIMEOUT_SEC = 60

# 集計に使うobd_dataカラム（domain/obd.hのcamelCase→Glueのsnake_caseは
# infra/lambda_src/obd_ingest/index.pyの_FIELD_MAPを参照）
_SELECT_COLS = [
    "obd_ts", "lat", "lon", "maf_gs", "fuel_rate_lph",
    "ltft_pct", "stft_pct", "catalyst_temp_c", "boost_kpa", "coolant_c",
]


def _open_sessions() -> list[dict]:
    """open-index GSIをQueryし、未処理セッション（open_marker="OPEN"）を全件返す。"""
    items = []
    kwargs = {
        "IndexName": "open-index",
        "KeyConditionExpression": "open_marker = :m",
        "ExpressionAttributeValues": {":m": "OPEN"},
    }
    while True:
        resp = watermark_table.query(**kwargs)
        items.extend(resp.get("Items", []))
        if "LastEvaluatedKey" not in resp:
            break
        kwargs["ExclusiveStartKey"] = resp["LastEvaluatedKey"]
    return items


def _is_session_ended(item: dict, now: int) -> bool:
    last_ts = int(item["last_ts"])
    if now - last_ts >= GAP_TIMEOUT_SEC:
        return True
    if not item.get("last_valid", True) and "invalid_since" in item:
        if now - int(item["invalid_since"]) >= INVALID_TAIL_SEC:
            return True
    return False


def _partition_filters(start_ts: int, end_ts: int) -> list[str]:
    """start_ts〜end_ts（epoch秒）をカバーする最小限のパーティションフィルタを返す。"""
    start_dt = datetime.fromtimestamp(start_ts, tz=timezone.utc).replace(minute=0, second=0, microsecond=0)
    end_dt = datetime.fromtimestamp(end_ts, tz=timezone.utc)

    years, months, days, hours = set(), set(), set(), set()
    cur = start_dt
    while cur <= end_dt:
        years.add(cur.strftime("%Y"))
        months.add(cur.strftime("%m"))
        days.add(cur.strftime("%d"))
        hours.add(cur.strftime("%H"))
        cur += timedelta(hours=1)

    def _in(col, vals):
        v = sorted(vals)
        return f"{col} = '{v[0]}'" if len(v) == 1 else f"{col} IN ({', '.join(repr(x) for x in v)})"

    return [_in("year", years), _in("month", months), _in("day", days), _in("hour", hours)]


def _run_athena_query(query: str) -> list[dict]:
    """クエリを投げてSUCCEEDEDになるまで同一Lambda呼び出し内でポーリングし、結果を返す。
    このリポジトリの他Lambda（query/index.py）はクライアント側ポーリング方式のため
    Lambda内ポーリングループの前例が無く、ここで新規実装する。"""
    resp = athena.start_query_execution(
        QueryString=query,
        QueryExecutionContext={"Database": ATHENA_DATABASE},
        WorkGroup=ATHENA_WORKGROUP,
    )
    execution_id = resp["QueryExecutionId"]

    deadline = time.time() + ATHENA_POLL_TIMEOUT_SEC
    while True:
        status = athena.get_query_execution(QueryExecutionId=execution_id)
        state = status["QueryExecution"]["Status"]["State"]
        if state == "SUCCEEDED":
            break
        if state in ("FAILED", "CANCELLED"):
            reason = status["QueryExecution"]["Status"].get("StateChangeReason", "")
            raise RuntimeError(f"Athena query {state}: {reason}")
        if time.time() > deadline:
            raise TimeoutError(f"Athena query timed out (execution_id={execution_id})")
        time.sleep(ATHENA_POLL_INTERVAL_SEC)

    return _parse_athena_results(execution_id)


def _parse_athena_results(execution_id: str) -> list[dict]:
    rows = []
    col_names = None
    kwargs = {"QueryExecutionId": execution_id}

    while True:
        result = athena.get_query_results(**kwargs)
        result_rows = result["ResultSet"]["Rows"]

        if col_names is None:
            col_names = [c["Label"] for c in result["ResultSet"]["ResultSetMetadata"]["ColumnInfo"]]
            result_rows = result_rows[1:]  # ヘッダー行スキップ

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


def _query_obd_data(device_id: str, start_ts: int, end_ts: int) -> list[dict]:
    filters = _partition_filters(start_ts, end_ts) + [
        f"device_id = '{device_id}'",
        f"obd_ts BETWEEN {start_ts} AND {end_ts}",
    ]
    query = f"SELECT {', '.join(_SELECT_COLS)} FROM obd_data WHERE " + " AND ".join(filters) + " ORDER BY obd_ts"
    return _run_athena_query(query)


def _haversine_m(lat1, lon1, lat2, lon2) -> float:
    import math

    r = 6371000
    p1, p2 = math.radians(lat1), math.radians(lat2)
    dphi = math.radians(lat2 - lat1)
    dlmb = math.radians(lon2 - lon1)
    a = math.sin(dphi / 2) ** 2 + math.cos(p1) * math.cos(p2) * math.sin(dlmb / 2) ** 2
    return 2 * r * math.asin(math.sqrt(a))


def _avg(vals: list[float]):
    return sum(vals) / len(vals) if vals else None


def _compute_summary(rows: list[dict]) -> dict:
    """obd_ts昇順のrowsから、距離（GPS積算）・燃料消費（台形積分）・
    LTFT/STFT平均・触媒温度/ブースト最大値・冷却水温の始点終点を算出する。"""
    rows = sorted(rows, key=lambda r: r["obd_ts"])

    distance_m = 0.0
    fuel_l = 0.0
    for i in range(1, len(rows)):
        prev, cur = rows[i - 1], rows[i]
        if prev.get("lat") is not None and cur.get("lat") is not None:
            distance_m += _haversine_m(prev["lat"], prev["lon"], cur["lat"], cur["lon"])
        dt = cur["obd_ts"] - prev["obd_ts"]
        r0 = prev.get("fuel_rate_lph") or 0.0
        r1 = cur.get("fuel_rate_lph") or 0.0
        fuel_l += (r0 + r1) / 2 * dt / 3600.0

    ltft = [r["ltft_pct"] for r in rows if r.get("ltft_pct") is not None]
    stft = [r["stft_pct"] for r in rows if r.get("stft_pct") is not None]
    catalyst = [r["catalyst_temp_c"] for r in rows if r.get("catalyst_temp_c") is not None]
    boost = [r["boost_kpa"] for r in rows if r.get("boost_kpa") is not None]
    coolant = [r["coolant_c"] for r in rows if r.get("coolant_c") is not None]

    distance_km = distance_m / 1000.0
    duration_sec = int(rows[-1]["obd_ts"] - rows[0]["obd_ts"])

    return {
        "distance_km": distance_km,
        "duration_sec": duration_sec,
        "fuel_l": fuel_l,
        "fuel_economy_km_l": (distance_km / fuel_l) if fuel_l > 0 else None,
        "ltft_avg": _avg(ltft),
        "stft_avg": _avg(stft),
        "catalyst_temp_max": max(catalyst) if catalyst else None,
        "boost_kpa_max": max(boost) if boost else None,
        "coolant_start": coolant[0] if coolant else None,
        "coolant_end": coolant[-1] if coolant else None,
    }


def _save_trip_summary(device_id: str, session_start: int, session_end: int, summary: dict) -> None:
    item = {
        "device_id": device_id,
        "session_start": session_start,
        "session_end": session_end,
        "narrative": "",  # Bedrockナラティブ生成は後続PRで実装
        "created_at": int(time.time()),
    }
    for k, v in summary.items():
        if v is None:
            continue
        # DynamoDB（boto3 resource層）はfloatを受け付けないためDecimalへ変換する。
        # str()経由にするのはfloat→Decimalの直接変換で生じる二進浮動小数点誤差を避けるため
        item[k] = Decimal(str(v)) if isinstance(v, float) else v
    trip_summary_table.put_item(Item=item)


def _close_watermark(device_id: str) -> None:
    watermark_table.update_item(
        Key={"device_id": device_id},
        UpdateExpression="REMOVE open_marker, session_start, invalid_since",
    )


def handler(event, context):
    now = int(time.time())
    processed = 0
    skipped = 0

    for item in _open_sessions():
        device_id = item["device_id"]
        if not _is_session_ended(item, now):
            continue

        session_start = item.get("session_start")
        last_ts = int(item["last_ts"])

        if session_start is not None:
            try:
                rows = _query_obd_data(device_id, int(session_start), last_ts)
                if rows:
                    summary = _compute_summary(rows)
                    _save_trip_summary(device_id, int(session_start), last_ts, summary)
                    processed += 1
                    print(f"[OK] {device_id}: trip {session_start}-{last_ts} summarized ({len(rows)} rows)")
                else:
                    print(f"[OK] {device_id}: trip {session_start}-{last_ts} had no obd_data rows, skipping summary")
            except Exception as e:
                print(f"[ERROR] {device_id}: analysis failed, will retry next sweep: {e}")
                continue  # watermarkをcloseせず次回リトライ
        else:
            skipped += 1  # session_start未設定（一度もvalid=trueが無い）、分析対象なし

        _close_watermark(device_id)

    print(f"[OK] sweep done: processed={processed} skipped={skipped}")
    return {"processed": processed, "skipped": skipped}
