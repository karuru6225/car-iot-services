"""
OBDトリップ分析（手動トリガー・S3ベース）

DynamoDB watermarkテーブル + 1分ごとのEventBridge Schedulerでリアルタイムに
トリップ終了を検知する旧設計（旧PR #28・#34、いずれもclose済み）は、複数Lambdaが
同じDynamoDBアイテムを非アトミックにread-modify-writeする競合状態でトリップが
サイレントに欠損するバグがあったため廃止した。

新設計: 管理画面から手動で「分析開始」を押したときだけ、以下を行う。
  1. S3に保存済みのトリップ結果一覧（追記のみの不変データ）から「どこまで分析済みか」を
     逆算する（DynamoDB等の可変ステートは持たない）
  2. 分析済み範囲より後のOBDデータをAthenaで取得する
  3. obd_ts昇順に並べ、連続する行の間隔がGAP_TIMEOUT_SECを超えた箇所でトリップに分割する
  4. 各トリップを集計し、S3に保存する

API Gatewayの29秒固定タイムアウト制約を避けるため、重い処理（Athenaクエリ〜集計〜複数
S3書き込み）はLambda self-invoke（InvocationType="Event"）で非同期に実行する。

以下のヘルパーはservice/query/index.py・trip_sweep/index.py（DynamoDB版、closeされた
PRの残骸でmainには存在しない）とロジックが重複するが、このリポジトリにLambda Layer等の
共有機構が無いため、今回は共通化せず重複を許容する（Rule of Three: 4つ目の類似Lambdaが
必要になった時点でLayer化を検討する）:
  _partition_filters / _run_athena_query / _parse_athena_results
    ... query/index.pyの_partition_filters_range・_parse_resultsと同種
  _haversine_m / _avg / _compute_summary
    ... trip_sweep/index.pyから移植
"""

import json
import os
import re
import time
from datetime import datetime, timedelta, timezone
from itertools import pairwise

import boto3

athena = boto3.client("athena")
s3 = boto3.client("s3")
lambda_client = boto3.client("lambda")

S3_BUCKET = os.environ["S3_BUCKET"]
ATHENA_DATABASE = os.environ["ATHENA_DATABASE"]
ATHENA_WORKGROUP = os.environ["ATHENA_WORKGROUP"]
SELF_FUNCTION_NAME = os.environ["SELF_FUNCTION_NAME"]

ATHENA_POLL_INTERVAL_SEC = 2.0
ATHENA_POLL_TIMEOUT_SEC = 240

TRIP_PREFIX = "trip-analysis"
JOB_PREFIX = "trip-analysis-jobs"

# 直近ジョブがRUNNINGのまま経過してよい最大時間。これを超えたらワーカーが異常終了した
# とみなし新規ジョブの起動を許可する自己修復（DynamoDBのようなロックを持たないための代替策）
RECENT_JOB_GUARD_SEC = 900

# 分析ロジック（_compute_summary等）のバージョン。キーに埋め込み、将来アルゴリズムを
# 改善した際に新バージョンの結果を旧バージョンと共存させたまま保存できるようにする
ANALYSIS_VERSION = 1

GAP_TIMEOUT_SEC = int(os.environ.get("GAP_TIMEOUT_SEC", "600"))
MIN_TRIP_DURATION_SEC = int(os.environ.get("MIN_TRIP_DURATION_SEC", "30"))


def _partition_filters(start_ts: int, end_ts: int) -> list[str]:
    """start_ts〜end_ts（epoch秒）をカバーする最小限のパーティションフィルタを返す。
    query/index.pyの_partition_filters_range・trip_sweep/index.pyの_partition_filtersと
    ロジックが重複するが、モジュール冒頭docstring記載の方針により共通化しない。"""
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


_TRIP_KEY_SEQ_RE = re.compile(r"_v\d+_(\d+)\.json$")


def _save_trip(device_id: str, start_ts: int, end_ts: int, summary: dict, row_count: int) -> None:
    """トリップ集計結果をS3へ保存する。session_end(UTC)基準でyear/monthパーティション化し、
    同一区間・同一ANALYSIS_VERSIONの既存ファイル数から次のseqを採番する
    （将来の「分析再実行」機能で同じ区間を再計算しても上書きせず新バージョンとして残せるようにする布石）。"""
    dt = datetime.fromtimestamp(end_ts, tz=timezone.utc)
    prefix = (
        f"{TRIP_PREFIX}/{device_id}/year={dt.year:04d}/month={dt.month:02d}/"
        f"{start_ts:010d}_{end_ts:010d}_v{ANALYSIS_VERSION:02d}_"
    )

    resp = s3.list_objects_v2(Bucket=S3_BUCKET, Prefix=prefix)
    existing_seqs = []
    for obj in resp.get("Contents", []):
        m = _TRIP_KEY_SEQ_RE.search(obj["Key"])
        if m:
            existing_seqs.append(int(m.group(1)))
    seq = max(existing_seqs, default=0) + 1
    key = f"{prefix}{seq:03d}.json"

    item = {
        "device_id": device_id,
        "session_start": start_ts,
        "session_end": end_ts,
        "row_count": row_count,
        "narrative": "",  # Bedrockナラティブ生成は将来実装
        "created_at": int(time.time()),
    }
    for k, v in summary.items():
        if v is not None:
            item[k] = v

    s3.put_object(Bucket=S3_BUCKET, Key=key, Body=json.dumps(item).encode(), ContentType="application/json")


def _latest_common_prefix(prefix: str) -> str | None:
    """prefix直下のフォルダ（Delimiter="/"のCommonPrefixes）のうち辞書順で最大のものを返す。
    S3のList系APIは常に辞書順でソートされるため、ページを最後まで辿った時点の最後の要素が最大値になる。
    全件を保持しないので、フォルダ数が多くてもメモリ・レイテンシに影響しない。"""
    kwargs = {"Bucket": S3_BUCKET, "Prefix": prefix, "Delimiter": "/"}
    latest = None
    while True:
        resp = s3.list_objects_v2(**kwargs)
        common_prefixes = resp.get("CommonPrefixes", [])
        if common_prefixes:
            latest = common_prefixes[-1]["Prefix"]
        if not resp.get("IsTruncated"):
            break
        kwargs["ContinuationToken"] = resp["NextContinuationToken"]
    return latest


_TRIP_FILENAME_RE = re.compile(r"^(\d{10})_(\d{10})_v(\d+)_(\d+)\.json$")


def _latest_session_end(device_id: str) -> int | None:
    """前回どこまで分析済みかを、S3に保存済みのトリップ結果一覧から逆算する（全件リストしない）。
    year→month→ファイルの3段階でドリルダウンする。"""
    year_prefix = _latest_common_prefix(f"{TRIP_PREFIX}/{device_id}/")
    if year_prefix is None:
        return None
    month_prefix = _latest_common_prefix(year_prefix)
    if month_prefix is None:
        return None

    resp = s3.list_objects_v2(Bucket=S3_BUCKET, Prefix=month_prefix)
    keys = sorted(o["Key"] for o in resp.get("Contents", []))
    if not keys:
        return None

    filename = keys[-1].rsplit("/", 1)[-1]
    m = _TRIP_FILENAME_RE.match(filename)
    if not m:
        return None
    return int(m.group(2))


def _list_partition_values(prefix: str, key_name: str) -> list[str]:
    """S3 Hive パーティション（key_name=value/）のバリュー一覧を返す。query/index.pyから移植。"""
    values = []
    kwargs = {"Bucket": S3_BUCKET, "Prefix": prefix, "Delimiter": "/"}
    while True:
        resp = s3.list_objects_v2(**kwargs)
        for cp in resp.get("CommonPrefixes", []):
            part = cp["Prefix"].rstrip("/").split("/")[-1]
            if "=" in part and part.split("=")[0] == key_name:
                values.append(part.split("=")[1])
        if not resp.get("IsTruncated"):
            break
        kwargs["ContinuationToken"] = resp["NextContinuationToken"]
    return sorted(values)


def _earliest_obd_ts() -> int | None:
    """obd/パーティションから最古のタイムスタンプ（epoch秒）を返す。
    query/index.pyの_get_data_range内の_earliestと同種のロジック。"""
    obd_prefix = "obd/"
    years = _list_partition_values(obd_prefix, "year")
    if not years:
        return None
    year = years[0]
    months = _list_partition_values(f"{obd_prefix}year={year}/", "month")
    if not months:
        return None
    month = months[0]
    days = _list_partition_values(f"{obd_prefix}year={year}/month={month}/", "day")
    if not days:
        return None
    day = days[0]
    hours = _list_partition_values(f"{obd_prefix}year={year}/month={month}/day={day}/", "hour")
    hour = hours[0] if hours else "00"
    dt = datetime(int(year), int(month), int(day), int(hour), tzinfo=timezone.utc)
    return int(dt.timestamp())


def _write_job_status(device_id: str, job_id: str, started_at: int, status: dict) -> None:
    key = f"{JOB_PREFIX}/{device_id}/{started_at:010d}_{job_id}.json"
    s3.put_object(Bucket=S3_BUCKET, Key=key, Body=json.dumps(status).encode(), ContentType="application/json")


def _read_job_status(device_id: str, job_id: str) -> dict | None:
    """job_idだけでキー（started_atを含む）を特定できないため、device_id配下を検索する。
    ジョブファイルは7日でexpireするため（infra/s3.tf）件数は少なく、全件走査でも問題ない。"""
    prefix = f"{JOB_PREFIX}/{device_id}/"
    resp = s3.list_objects_v2(Bucket=S3_BUCKET, Prefix=prefix)
    for obj in resp.get("Contents", []):
        if obj["Key"].endswith(f"_{job_id}.json"):
            body = s3.get_object(Bucket=S3_BUCKET, Key=obj["Key"])["Body"].read()
            return json.loads(body)
    return None


def _find_recent_running_job(device_id: str) -> dict | None:
    """直近ジョブがRECENT_JOB_GUARD_SEC以内にRUNNINGならそれを返す（二重起動防止）。
    ガード時間を超えてRUNNINGのままの場合はワーカーが異常終了したとみなし、
    新規ジョブの起動を許可する（自己修復）。"""
    prefix = f"{JOB_PREFIX}/{device_id}/"
    resp = s3.list_objects_v2(Bucket=S3_BUCKET, Prefix=prefix)
    keys = sorted(o["Key"] for o in resp.get("Contents", []))
    if not keys:
        return None
    body = s3.get_object(Bucket=S3_BUCKET, Key=keys[-1])["Body"].read()
    status = json.loads(body)
    if status.get("status") == "RUNNING" and int(time.time()) - status["started_at"] < RECENT_JOB_GUARD_SEC:
        return status
    return None


def _run_athena_query(query: str) -> list[dict]:
    """クエリを投げてSUCCEEDEDになるまで同一Lambda呼び出し内でポーリングし、結果を返す。
    trip_sweep/index.pyから移植（このリポジトリの他Lambdaはクライアント側ポーリング方式のため
    Lambda内ポーリングループの前例がここで新規実装された）。"""
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
    """クエリ結果を dict のリストに変換する。数値は float に変換する。
    query/index.pyの_parse_results・trip_sweep/index.pyの_parse_athena_resultsと
    ロジックが重複するが、モジュール冒頭docstring記載の方針により共通化しない。"""
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


def _split_trips(rows: list[dict], gap_sec: int = GAP_TIMEOUT_SEC) -> list[list[dict]]:
    """obd_ts昇順のrowsを、連続する行のobd_ts間隔がgap_secを超えた箇所でトリップに分割する。"""
    if not rows:
        return []
    trips = [[rows[0]]]
    for prev, cur in pairwise(rows):
        if cur["obd_ts"] - prev["obd_ts"] > gap_sec:
            trips.append([])
        trips[-1].append(cur)
    return trips


# trip_sweep/index.py（DynamoDB版、closeされたPRの残骸でmainには存在しない）から移植
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
    for prev, cur in pairwise(rows):
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
