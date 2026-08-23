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
import uuid
from datetime import datetime, timedelta, timezone
from itertools import pairwise

import boto3

athena = boto3.client("athena")
s3 = boto3.client("s3")
lambda_client = boto3.client("lambda")
location_client = boto3.client("location")
bedrock_client = boto3.client("bedrock-runtime")

S3_BUCKET = os.environ["S3_BUCKET"]
ATHENA_DATABASE = os.environ["ATHENA_DATABASE"]
ATHENA_WORKGROUP = os.environ["ATHENA_WORKGROUP"]
SELF_FUNCTION_NAME = os.environ["SELF_FUNCTION_NAME"]

# AIナレーティブ生成用（位置情報・Bedrock）
HOME_LAT = float(os.environ["HOME_LAT"])
HOME_LON = float(os.environ["HOME_LON"])
HOME_RADIUS_M = float(os.environ.get("HOME_RADIUS_M", "50"))
PLACE_INDEX_NAME = os.environ["PLACE_INDEX_NAME"]
BEDROCK_MODEL_ID = os.environ["BEDROCK_MODEL_ID"]

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

# 1回のジョブで処理する最大時間幅。データが長期間分析されずに溜まっていても
# Athenaクエリ対象期間・Lambda実行時間を有限に保つため、超過分は次回のジョブに持ち越す
MAX_WINDOW_HOURS = 24 * 14

DEVICE_ID_RE = re.compile(r"^[A-Za-z0-9_-]{1,64}$")  # S3キーインジェクション対策（obd_ingest/index.pyと同一パターン）


def _resp(status: int, body: dict) -> dict:
    return {
        "statusCode": status,
        "headers": {"Content-Type": "application/json"},
        "body": json.dumps(body, ensure_ascii=False),
    }


def _err(status: int, msg: str) -> dict:
    return _resp(status, {"error": msg})


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


def _save_trip(device_id: str, start_ts: int, end_ts: int, summary: dict, row_count: int, narrative: str = "") -> None:
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
        "narrative": narrative,
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


def _list_common_prefixes(prefix: str) -> list[str]:
    """prefix直下のフォルダ（Delimiter="/"のCommonPrefixes）を全件返す。
    年月フォルダ数は1デバイスあたり高々数十件程度で全件取得しても問題にならない想定
    （月内のトリップファイル自体が多い場合は_load_trips側でlimit件到達時点で打ち切る）。"""
    kwargs = {"Bucket": S3_BUCKET, "Prefix": prefix, "Delimiter": "/"}
    result = []
    while True:
        resp = s3.list_objects_v2(**kwargs)
        result.extend(cp["Prefix"] for cp in resp.get("CommonPrefixes", []))
        if not resp.get("IsTruncated"):
            break
        kwargs["ContinuationToken"] = resp["NextContinuationToken"]
    return result


def _load_trips(device_id: str, limit: int = 200) -> list[dict]:
    """trip-analysis/{device_id}/配下のトリップ一覧を新しい順に最大limit件返す。
    年月フォルダ・ファイルを新しい順に辿りながらlimit件集まった時点で打ち切るため、
    全トリップ数が多いデバイスでも読み込むオブジェクト数はlimitに収まる。"""
    device_prefix = f"{TRIP_PREFIX}/{device_id}/"
    year_prefixes = sorted(_list_common_prefixes(device_prefix), reverse=True)

    trips = []
    for year_prefix in year_prefixes:
        month_prefixes = sorted(_list_common_prefixes(year_prefix), reverse=True)
        for month_prefix in month_prefixes:
            resp = s3.list_objects_v2(Bucket=S3_BUCKET, Prefix=month_prefix)
            keys = sorted((o["Key"] for o in resp.get("Contents", [])), reverse=True)
            for key in keys:
                if not _TRIP_FILENAME_RE.match(key.rsplit("/", 1)[-1]):
                    continue
                obj = s3.get_object(Bucket=S3_BUCKET, Key=key)
                trip = json.loads(obj["Body"].read())
                trip["analysis_key"] = key  # narrativeの個別再生成（_handle_regenerate_narrative）が対象を特定するために使う
                trips.append(trip)
                if len(trips) >= limit:
                    return trips
    return trips


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


def _handle_start(event: dict) -> dict:
    """POST /trip-analysis {device_id} — 未分析期間のジョブを作成し、自己非同期呼び出しして即returnする。"""
    body = json.loads(event.get("body") or "{}")
    device_id = body.get("device_id", "")
    if not DEVICE_ID_RE.match(device_id):
        return _err(400, "invalid device_id")

    running = _find_recent_running_job(device_id)
    if running is not None:
        return _resp(200, running)

    last_end = _latest_session_end(device_id)
    start_ts = (last_end + 1) if last_end is not None else _earliest_obd_ts()
    now = int(time.time())
    if start_ts is None or start_ts >= now:
        return _resp(200, {"status": "SUCCEEDED", "trips_saved": 0, "has_more": False})

    end_ts = min(now, start_ts + MAX_WINDOW_HOURS * 3600)
    job_id = uuid.uuid4().hex
    status = {
        "job_id": job_id,
        "device_id": device_id,
        "status": "RUNNING",
        "started_at": now,
        "finished_at": None,
        "range": {"start_ts": start_ts, "end_ts": end_ts},
        "trips_saved": 0,
        "has_more": end_ts < now,
        "error": None,
    }
    _write_job_status(device_id, job_id, now, status)

    lambda_client.invoke(
        FunctionName=SELF_FUNCTION_NAME,
        InvocationType="Event",
        Payload=json.dumps(
            {
                "trip_analysis_job": True,
                "job_id": job_id,
                "device_id": device_id,
                "start_ts": start_ts,
                "end_ts": end_ts,
                "started_at": now,
            }
        ).encode(),
    )
    return _resp(200, status)


_SELECT_COLS = [
    "obd_ts", "lat", "lon", "fuel_rate_lph",
    "ltft_pct", "stft_pct", "catalyst_temp_c", "boost_kpa", "coolant_c",
    "rpm", "throttle_pct", "speed_kmh", "timing_deg", "iat_c", "ecu_voltage", "load_pct",
]


def _query_obd_data(device_id: str, start_ts: int, end_ts: int) -> list[dict]:
    """trip_sweep/index.pyの_query_obd_dataと同種（モジュール冒頭docstring記載の方針により共通化しない）。"""
    filters = _partition_filters(start_ts, end_ts) + [
        f"device_id = '{device_id}'",
        f"obd_ts BETWEEN {start_ts} AND {end_ts}",
    ]
    query = f"SELECT {', '.join(_SELECT_COLS)} FROM obd_data WHERE " + " AND ".join(filters) + " ORDER BY obd_ts"
    return _run_athena_query(query)


def _process_job(job_id: str, device_id: str, start_ts: int, end_ts: int, started_at: int) -> None:
    """self-invokeされた側の重い処理本体。Athenaクエリ〜トリップ分割〜集計〜複数S3書き込みを行い、
    最後にジョブ状態をSUCCEEDED/FAILEDで更新する（_handle_startが書いたrange/has_more等は保持する）。"""
    existing = _read_job_status(device_id, job_id) or {}
    try:
        rows = _query_obd_data(device_id, start_ts, end_ts)
        trips = _split_trips(rows, gap_sec=GAP_TIMEOUT_SEC)

        now = int(time.time())
        trips_saved = 0
        for i, trip_rows in enumerate(trips):
            trip_start = trip_rows[0]["obd_ts"]
            trip_end = trip_rows[-1]["obd_ts"]
            is_last = i == len(trips) - 1
            if is_last and now - trip_end < GAP_TIMEOUT_SEC:
                continue  # 走行中の可能性があるため未確定として次回に持ち越す
            if trip_end - trip_start < MIN_TRIP_DURATION_SEC:
                continue  # ノイズとして破棄
            summary = _compute_summary(trip_rows)
            _save_trip(device_id, int(trip_start), int(trip_end), summary, row_count=len(trip_rows))
            trips_saved += 1

        status = {
            **existing,
            "status": "SUCCEEDED",
            "finished_at": int(time.time()),
            "trips_saved": trips_saved,
            "error": None,
        }
    except Exception as e:
        status = {
            **existing,
            "status": "FAILED",
            "finished_at": int(time.time()),
            "error": str(e),
        }
    _write_job_status(device_id, job_id, started_at, status)


def _handle_get(event: dict) -> dict:
    """GET /trip-analysis?device_id=&job_id= — job_id指定時はジョブ状態、無指定時はトリップ一覧を返す。"""
    params = event.get("queryStringParameters") or {}
    device_id = params.get("device_id", "")
    if not DEVICE_ID_RE.match(device_id):
        return _err(400, "invalid device_id")

    job_id = params.get("job_id")
    if job_id:
        status = _read_job_status(device_id, job_id)
        if status is None:
            return _err(404, "job not found")
        return _resp(200, status)

    return _resp(200, {"trips": _load_trips(device_id)})


def _valid_trip_key(device_id: str, key: str) -> bool:
    """narrative個別再生成APIが受け取ったkeyが、指定device_id配下の実在しうるトリップ
    ファイル名パターンに一致するかを検証する（他device_idのファイルを指定させない・
    S3キーインジェクション対策）。"""
    prefix = f"{TRIP_PREFIX}/{device_id}/"
    return key.startswith(prefix) and bool(_TRIP_FILENAME_RE.match(key.rsplit("/", 1)[-1]))


def _regenerate_trip_narrative(device_id: str, key: str) -> dict:
    """指定トリップのnarrativeを（再）生成し、同じS3キーへ上書き保存する。
    _process_job()からは自動実行しない（Web管理画面から個別に呼び出す想定）ため、
    新規・過去問わず任意のトリップに対して何度でも実行できる。

    集計値（distance_km/coolant_start等）も_compute_summary()で計算し直してから
    保存する。過去に保存されたトリップは、保存当時の_compute_summary()のロジックで
    計算された値のままになっている（例: _is_comm_dropout()導入前に保存されたトリップは
    通信断行のcoolant_c=0を拾ったまま）ため、生データを再取得するこの機会に最新ロジックの
    値へ更新し、ナレーティブに古い集計値由来の不整合（「冷却水温は終始0.0°C」等）を
    渡さないようにする。"""
    obj = s3.get_object(Bucket=S3_BUCKET, Key=key)
    trip = json.loads(obj["Body"].read())

    rows = _query_obd_data(device_id, trip["session_start"], trip["session_end"])
    summary = _compute_summary(rows)
    for k, v in summary.items():
        if v is not None:
            trip[k] = v
    trip["narrative"] = _generate_narrative(device_id, trip["session_start"], trip["session_end"], rows, trip)

    s3.put_object(Bucket=S3_BUCKET, Key=key, Body=json.dumps(trip).encode(), ContentType="application/json")
    trip["analysis_key"] = key
    return trip


def _handle_regenerate_narrative(event: dict) -> dict:
    """POST /trip-analysis/narrative {device_id, key} — 指定トリップのnarrativeを（再）生成する。"""
    body = json.loads(event.get("body") or "{}")
    device_id = body.get("device_id", "")
    key = body.get("key", "")
    if not DEVICE_ID_RE.match(device_id):
        return _err(400, "invalid device_id")
    if not _valid_trip_key(device_id, key):
        return _err(400, "invalid key")

    try:
        trip = _regenerate_trip_narrative(device_id, key)
    except s3.exceptions.NoSuchKey:
        return _err(404, "trip not found")
    return _resp(200, trip)


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


def _std(vals: list[float]):
    """母集団標準偏差（分母n）。AIナレーティブ生成のベースラインpooling用。"""
    if not vals:
        return None
    m = _avg(vals)
    return (sum((v - m) ** 2 for v in vals) / len(vals)) ** 0.5


def _is_comm_dropout(row: dict) -> bool:
    """イグニッションOFF直前・アイドリングストップ切替瞬間に、通信断で複数センサー値が
    同時にゼロ埋めされる実データ上のパターンを検出する（AIナレーティブ生成のPOCで確認済み）。
    coolant_c/ecu_voltageが物理的にありえない組み合わせで同時にゼロになる行だけを対象にし、
    他フィールドの集計には影響させない狭いスコープの判定。"""
    return row.get("coolant_c") == 0 and row.get("ecu_voltage") == 0


def _compute_summary(rows: list[dict]) -> dict:
    """obd_ts昇順のrowsから、距離（GPS積算）・燃料消費（台形積分）・
    LTFT/STFT平均・触媒温度/ブースト最大値・冷却水温の始点終点を算出する。
    boost_kpa/ltft_pct/stft_pct/timing_degのavg/stdは、AIナレーティブ生成の
    ベースラインpooling（_compute_row_baseline）用に追加した。"""
    rows = sorted(rows, key=lambda r: r["obd_ts"])

    distance_m = 0.0
    fuel_l = 0.0
    for prev, cur in pairwise(rows):
        # 通信断行を挟む区間は距離・燃料どちらも積算しない。通信断の前後は往々にして
        # 長時間のデータ欠落（DeepSleepからの復帰待ち等）を伴い、GPS座標だけは前後の
        # 行に残っているため、そこで実際に移動していたとしても燃料側はfuel_rate_lph=0の
        # 区間として積分され「距離はあるのに燃料がほぼゼロ」という不整合な燃費が生じる
        # （実データで確認済み、esp32_iot_gateway/CONTEXT.mdの該当TODO参照）
        if _is_comm_dropout(prev) or _is_comm_dropout(cur):
            continue
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
    timing = [r["timing_deg"] for r in rows if r.get("timing_deg") is not None]
    # 通信断でゼロ埋めされた行はcoolant_c/ecu_voltageの集計からのみ除外する
    coolant = [r["coolant_c"] for r in rows if r.get("coolant_c") is not None and not _is_comm_dropout(r)]

    distance_km = distance_m / 1000.0
    duration_sec = int(rows[-1]["obd_ts"] - rows[0]["obd_ts"])

    return {
        "distance_km": distance_km,
        "duration_sec": duration_sec,
        "fuel_l": fuel_l,
        "fuel_economy_km_l": (distance_km / fuel_l) if fuel_l > 0 else None,
        "ltft_avg": _avg(ltft),
        "ltft_std": _std(ltft),
        "stft_avg": _avg(stft),
        "stft_std": _std(stft),
        "catalyst_temp_max": max(catalyst) if catalyst else None,
        "boost_kpa_max": max(boost) if boost else None,
        "boost_kpa_avg": _avg(boost),
        "boost_kpa_std": _std(boost),
        "timing_deg_avg": _avg(timing),
        "timing_deg_std": _std(timing),
        "coolant_start": coolant[0] if coolant else None,
        "coolant_end": coolant[-1] if coolant else None,
    }


# ─── AIナレーティブ生成 ────────────────────────────────────────────────────────
# 30秒バケットの時系列＋車固有ベースラインからBedrockへの入力を組み立てる。
# 設計方針の詳細はesp32_iot_gateway/CONTEXT.mdの「OBDトリップのAIナレーティブ生成」TODO参照。

BUCKET_SEC = 30
# 変動が速く、バケットごとのmax/min/meanを見た方が意味を持つ項目
# load_pctは「速度がほぼ一定なのに負荷率が高い」ような走行パターン（上り坂等）を
# AIに時系列から直接読み取らせるために追加した（正常/異常のラベル付け対象ではない）
FAST_FIELDS = ["rpm", "boost_kpa", "throttle_pct", "speed_kmh", "ltft_pct", "stft_pct", "timing_deg", "iat_c", "load_pct"]
# ゆっくり変化し、バケットごとの平均だけでよい項目（通信断除外フィルタの対象でもある）
SLOW_FIELDS = ["coolant_c", "ecu_voltage"]

# 値が大きい方向(high)/小さい方向(low)がそれぞれ何を意味するかの事前マッピング。
# 「正常/異常」の概念になじむ項目だけに限定する（rpm/throttle_pct/speed_kmhはドライバー操作依存、
# iat_cはトレンドの方が意味を持つため対象外、というPOCでの検証結果を踏襲）。
FIELD_LABELS = {
    "ltft_pct": {"high": "薄め", "low": "濃いめ"},
    "stft_pct": {"high": "薄め", "low": "濃いめ"},
    "boost_kpa": {"high": "高め", "low": "低め"},
    "timing_deg": {"high": "進角気味", "low": "遅角気味"},
}

# _compute_row_baseline用: バケット項目名 → 保存済みトリップ集計値のavg/stdフィールド名
_ROW_BASELINE_FIELD_MAP = {
    "ltft_pct": ("ltft_avg", "ltft_std"),
    "stft_pct": ("stft_avg", "stft_std"),
    "boost_kpa": ("boost_kpa_avg", "boost_kpa_std"),
    "timing_deg": ("timing_deg_avg", "timing_deg_std"),
}

# _compute_trip_baseline用: トリップ全体の評価に使うフィールド
_TRIP_BASELINE_FIELDS = ["ltft_avg", "stft_avg", "boost_kpa_avg", "fuel_economy_km_l"]

# ベースライン計算対象から"幽霊トリップ"（GPS未捕捉でdistance_km≈0なのにduration/fuelは
# 正常値、というノイズ）を除外する距離下限。fuel_economy_km_l等の「距離が分母」の指標が
# 汚染されるためPOCで確認済み。
BASELINE_MIN_DISTANCE_KM = 0.5


def _bucket_rows(rows: list[dict], trip_start: int, bucket_sec: int = BUCKET_SEC) -> list[dict]:
    """obd_ts昇順のrowsをbucket_sec秒ごとに分割し、各バケットについて代表座標
    （バケット内先頭行のlat/lon）とFAST_FIELDSのmax/min/mean、SLOW_FIELDSのmean
    （通信断除外後）を算出する。"""
    buckets: dict[int, list[dict]] = {}
    for row in rows:
        idx = int((row["obd_ts"] - trip_start) // bucket_sec)
        buckets.setdefault(idx, []).append(row)

    result = []
    for idx in sorted(buckets):
        bucket_rows = buckets[idx]
        clean_rows = [r for r in bucket_rows if not _is_comm_dropout(r)]
        entry = {
            "t_sec": idx * bucket_sec,
            "lat": bucket_rows[0].get("lat"),
            "lon": bucket_rows[0].get("lon"),
        }
        for field in FAST_FIELDS:
            vals = [r[field] for r in bucket_rows if r.get(field) is not None]
            entry[f"{field}_max"] = max(vals) if vals else None
            entry[f"{field}_min"] = min(vals) if vals else None
            entry[f"{field}_mean"] = _avg(vals)
        for field in SLOW_FIELDS:
            vals = [r[field] for r in clean_rows if r.get(field) is not None]
            entry[f"{field}_mean"] = _avg(vals)
        result.append(entry)
    return result


def _weighted_mean_std(pairs: list[tuple[float, int]]) -> tuple[float, float] | tuple[None, None]:
    """(value, weight)のリストから重みつき平均・標準偏差を返す。
    weightにはrow_count（トリップの行数）や過去トリップの行数合計を渡す想定。"""
    total_w = sum(w for _, w in pairs if w > 0)
    if total_w == 0:
        return None, None
    mean = sum(v * w for v, w in pairs if w > 0) / total_w
    variance = sum(w * (v - mean) ** 2 for v, w in pairs if w > 0) / total_w
    return mean, variance ** 0.5


def _baseline_trips(past_trips: list[dict], min_distance_km: float = BASELINE_MIN_DISTANCE_KM) -> list[dict]:
    """ベースライン計算対象のトリップだけに絞る（"幽霊トリップ"除外）。"""
    return [t for t in past_trips if (t.get("distance_km") or 0.0) > min_distance_km]


def _compute_trip_baseline(past_trips: list[dict], min_distance_km: float = BASELINE_MIN_DISTANCE_KM) -> dict:
    """過去トリップの集計値（トリップ単位のスカラー）から、row_count重みつき平均・標準偏差を
    計算する。「今回のトリップ全体が普段と違うか」の評価に使う。"""
    candidates = _baseline_trips(past_trips, min_distance_km)
    result = {}
    for field in _TRIP_BASELINE_FIELDS:
        pairs = [(t[field], t["row_count"]) for t in candidates if t.get(field) is not None and t.get("row_count")]
        mean, std = _weighted_mean_std(pairs)
        if mean is not None:
            result[field] = {"mean": mean, "std": std, "n": sum(w for _, w in pairs)}
    return result


def _compute_row_baseline(past_trips: list[dict], min_distance_km: float = BASELINE_MIN_DISTANCE_KM) -> dict:
    """過去トリップに保存済みの行単位avg/std/row_countを、pooled varianceの合成公式で
    合成し、「全トリップの行データを1つにまとめて計算した場合」と同一の行単位mean/std/nを
    過去の生データを取り直さずに復元する。「トリップ内のこの瞬間が普段と違うか」
    （バケットのz-scoreラベル付け）に使う。

    pooled_mean = Σ(n_i * mean_i) / Σn_i
    pooled_var  = Σ(n_i * (std_i^2 + (mean_i - pooled_mean)^2)) / Σn_i
    """
    candidates = _baseline_trips(past_trips, min_distance_km)
    result = {}
    for field, (avg_key, std_key) in _ROW_BASELINE_FIELD_MAP.items():
        groups = [
            (t[avg_key], t[std_key], t["row_count"])
            for t in candidates
            if t.get(avg_key) is not None and t.get(std_key) is not None and t.get("row_count")
        ]
        total_n = sum(n for _, _, n in groups)
        if total_n == 0:
            continue
        pooled_mean = sum(n * mean for mean, _, n in groups) / total_n
        pooled_var = sum(n * (std**2 + (mean - pooled_mean) ** 2) for mean, std, n in groups) / total_n
        result[field] = {"mean": pooled_mean, "std": pooled_var ** 0.5, "n": total_n}
    return result


def _label_for_zscore(field: str, z: float | None) -> str:
    """z-scoreを、FIELD_LABELSのhigh/low表現＋帯（大きく/やや/通常どおり）を組み合わせた
    完成済みの自然言語ラベルに変換する。AIには生の数値・統計用語を渡さず、このラベルだけを渡す。"""
    labels = FIELD_LABELS[field]
    if z is None:
        return "通常どおり"
    if z >= 2:
        return f"大きく{labels['high']}"
    if z >= 1:
        return f"やや{labels['high']}"
    if z <= -2:
        return f"大きく{labels['low']}"
    if z <= -1:
        return f"やや{labels['low']}"
    return "通常どおり"


# 軽自動車・ターボ車の一般的な実燃費目安（車種依存のため参考程度、断定はしない）
_GENERAL_FUEL_ECONOMY_RANGE_KM_L = (15.0, 20.0)


def _fuel_economy_benchmark(fuel_economy_km_l: float | None) -> str:
    """車固有ベースラインとは別に、一般的な軽ターボ車の実燃費目安との比較も機械的に
    判定する。車固有だけだと「そもそも一般的にどうか」という素朴な感覚が抜け落ちるため
    （POCで確認済み）。数値の大小判定はAIに委ねずここで済ませる。"""
    if fuel_economy_km_l is None:
        return ""
    low, high = _GENERAL_FUEL_ECONOMY_RANGE_KM_L
    if fuel_economy_km_l < low:
        return f"一般的な軽ターボ車の目安（{low:.0f}〜{high:.0f}km/L程度）を下回っています"
    if fuel_economy_km_l > high:
        return f"一般的な軽ターボ車の目安（{low:.0f}〜{high:.0f}km/L程度）を上回っています"
    return f"一般的な軽ターボ車の目安（{low:.0f}〜{high:.0f}km/L程度）の範囲内です"


def _reverse_geocode(lat: float | None, lon: float | None) -> dict | None:
    """緯度経度をAWS Location Service（Here）で逆ジオコーディングし、地名文字列に変換する。
    LLMに生の緯度経度を渡すとハルシネーション（特に住宅地で誤った地名を自信満々に返す）の
    リスクが高いため、実データとして得た文字列だけを渡す方針（POCで検証済み）。
    自宅座標から半径HOME_RADIUS_M以内は逆ジオコーディングを呼ばず「自宅」と匿名化する。"""
    if lat is None or lon is None:
        return None
    if _haversine_m(lat, lon, HOME_LAT, HOME_LON) <= HOME_RADIUS_M:
        return {"kind": "home"}

    resp = location_client.search_place_index_for_position(
        IndexName=PLACE_INDEX_NAME, Position=[lon, lat], MaxResults=5, Language="ja"
    )
    results = resp.get("Results", [])
    if not results:
        return {"kind": "unknown"}

    place = results[0]["Place"]
    coarse = f"{place.get('Region', '')}{place.get('Municipality', '')}"
    nearby_poi = [
        r["Place"]["Label"].split(" ", 1)[-1]
        for r in results
        if "PointOfInterestType" in r["Place"].get("Categories", [])
    ][:3]
    return {"kind": "address", "coarse": coarse, "nearby_poi": nearby_poi}


def _describe_location(lat: float | None, lon: float | None) -> str:
    """プロンプトにそのまま埋め込める1行の地点説明文にする。"""
    geo = _reverse_geocode(lat, lon)
    if geo is None:
        return "位置情報が記録されていません"
    if geo["kind"] == "home":
        return "自宅"
    if geo["kind"] == "address":
        return f"{geo['coarse']}付近" if geo["coarse"] else "不明な地点付近"
    return "不明な地点"


def _clean_field_endpoints(rows: list[dict], field: str) -> tuple[float | None, float | None]:
    """obd_ts昇順のrowsから、通信断（_is_comm_dropout）を除外したfieldの始点・終点値を返す。
    _compute_summary()に保存しないフィールド（ecu_voltage等）の始点終点をナレーティブ生成時に
    その場で算出するために使う。"""
    rows = sorted(rows, key=lambda r: r["obd_ts"])
    vals = [r[field] for r in rows if r.get(field) is not None and not _is_comm_dropout(r)]
    return (vals[0], vals[-1]) if vals else (None, None)


def _fmt(v: float | None, precision: int = 1) -> str:
    return "" if v is None else f"{v:.{precision}f}"


# CSVの数値項目のうち、判定済みラベルを併記する項目（FIELD_LABELSと同じ4項目）
_LABELED_CSV_FIELDS = list(FIELD_LABELS)
# ラベルなし・生の統計値だけを参考情報として渡す項目
_UNLABELED_CSV_FIELDS = ["rpm", "throttle_pct", "speed_kmh", "iat_c", "load_pct"]


def _bucket_csv(buckets: list[dict], row_baseline: dict) -> str:
    """_bucket_rows()の出力を、行単位ベースラインに対するz-scoreラベルを付けたCSV文字列にする。
    ラベル付き項目は{field}_mean/{field}_level、ラベルなし項目は{field}_max/min/meanの列を持つ。"""
    header = ["lat", "lon"]
    for field in _LABELED_CSV_FIELDS:
        header += [f"{field}_mean", f"{field}_level"]
    for field in _UNLABELED_CSV_FIELDS:
        header += [f"{field}_max", f"{field}_min", f"{field}_mean"]
    lines = [",".join(header)]

    for b in buckets:
        row = [_fmt(b["lat"], 7), _fmt(b["lon"], 7)]
        for field in _LABELED_CSV_FIELDS:
            mean = b.get(f"{field}_mean")
            baseline = row_baseline.get(field)
            z = None
            if mean is not None and baseline and baseline["std"]:
                z = (mean - baseline["mean"]) / baseline["std"]
            row += [_fmt(mean), _label_for_zscore(field, z)]
        for field in _UNLABELED_CSV_FIELDS:
            row += [_fmt(b.get(f"{field}_max")), _fmt(b.get(f"{field}_min")), _fmt(b.get(f"{field}_mean"))]
        lines.append(",".join(row))
    return "\n".join(lines)


# |z|がこれ以上ならもはや「良い/悪い」ではなく計測・集計の異常を疑うべき値とみなす。
# 燃費のような比率指標（距離/燃料）はセンサー欠損等の影響を受けやすく、実データで
# 372km/Lのような物理的にありえない値が_compute_summary()側の別バグで発生した実例がある。
# 根本原因の修正（通信断ギャップの除外）だけに頼らず、この判定側でも保険をかけておく
_FUEL_ECONOMY_OUTLIER_Z = 3.0


def _fuel_economy_car_comparison(fuel_economy_km_l: float | None, trip_baseline: dict) -> str:
    """車固有ベースライン（_compute_trip_baseline）と比較した評価文。一般的な目安との比較
    （_fuel_economy_benchmark）だけだと「この車としてはどうか」が抜け落ちるため両方渡す。"""
    fb = trip_baseline.get("fuel_economy_km_l")
    if fuel_economy_km_l is None or not fb or not fb.get("std"):
        return "この車の実績と比較できるデータがまだ十分ではありません"
    z = (fuel_economy_km_l - fb["mean"]) / fb["std"]
    if abs(z) >= _FUEL_ECONOMY_OUTLIER_Z:
        return "この車の過去の実績から大きく外れた値です。走行距離や燃料消費量の計測・集計に問題があった可能性が高く、参考値として扱ってください"
    if z >= 1:
        return "この車の実績としては良め"
    if z <= -1:
        return "この車の実績としてはやや低め"
    return "この車の実績としては普段どおり"


_FIELD_LABEL_MEANINGS = """  - `ltft_pct`/`stft_pct`が「薄め」: 燃料噴射を減らす方向に補正されている状態。軽負荷の巡航等では
    自然に起こりうるが、大きく・継続的に薄い場合は吸気系のわずかな空気の漏れ込み等が背景にある
    こともある一般的な傾向
  - `boost_kpa`が「高め」: ターボの過給圧が普段より高い状態。加速や上り坂等、エンジンに大きな
    出力を求めた場面で自然に高くなる
  - `timing_deg`が「遅角気味」: 点火時期が普段より遅らせてある状態。エンジンが自己判断でノッキング
    (異常燃焼)を避けるために点火を遅らせている場合に起こりうる一般的な傾向
  - `timing_deg`が「進角気味」: 点火時期が普段より早めてある状態。効率よく燃焼できている時に
    起こりやすい
  - 度合い: 「通常どおり」＝いつもと変わらない、「やや◯◯」＝いつもよりわずかに◯◯の傾向、
    「大きく◯◯」＝いつもよりはっきり◯◯の傾向（この場合のみ「注意すべき数値」として触れる）"""


def _build_narrative_prompt(
    summary: dict,
    start_desc: str,
    end_desc: str,
    slow_endpoints: dict,
    trip_baseline: dict,
    csv_text: str,
) -> list[dict]:
    """experiments/trip-analysis-ai-poc/build_prompt_latest_trip.py（POCで確定した最終形）を
    そのまま実装したBedrock Converse API用messagesを組み立てる。渡す数値はすべて機械的に
    算出済みの事実のみで、Claude（検証時）の解釈を混入させない。"""
    coolant_start, coolant_end = slow_endpoints["coolant_c"]
    ecu_start, ecu_end = slow_endpoints["ecu_voltage"]
    fuel_economy = summary.get("fuel_economy_km_l")
    vs_car = _fuel_economy_car_comparison(fuel_economy, trip_baseline)
    vs_general = _fuel_economy_benchmark(fuel_economy)

    fuel_line = (
        f"燃費: {fuel_economy:.2f} km/L\n"
        f"  - この車の実績と比較した評価（判定済み、再解釈しないこと）: {vs_car}\n"
        f"  - 一般的な目安と比較した評価（判定済み、再解釈しないこと）: {vs_general}"
        if fuel_economy is not None
        else "燃費: 走行距離または燃料消費が記録されておらず算出できません"
    )

    prompt = f"""以下はホンダN-VAN(ターボ車)の1トリップ分のOBD-IIデータです。この車の過去の実績データ
(車固有のベースライン)も一緒に渡すので、単なる一般論ではなくこの車自身の傾向と比較した分析を
してください。①一言サマリー（開始地点・終了地点も含める） ②注意すべき数値の指摘 ③車に詳しくない
人にもわかる平易な説明 ④提案 ⑤走行の特徴 の5点を含む日本語レポート(400〜500字程度)でお願いします。

**④提案について（重要）**: 提案・アドバイス・「〜がおすすめです」「〜が必要かもしれません」の
ような文言は**④にのみ**書いてください。①②③の本文中には一切書かないでください。④に書く提案は、
②で「大きく」判定の項目に触れた場合のみ、具体的な次のアクションを1文程度で書いてください。
②で「大きく」判定が無かった場合、④は「特にありません」としてください。

**①一言サマリーについて**: 燃費・消費燃料は数値を明記し、下記の評価も一言添えてください。
それ以外（冷却水温・ECU電圧等）は数値を列挙せず、アバウトな一言に留めてください。

## トリップ全体の集計値
- 走行距離: {summary["distance_km"]:.2f} km / 推定消費燃料: {summary["fuel_l"]:.2f} L / {fuel_line}
- 開始地点: {start_desc}
- 終了地点: {end_desc}
- 冷却水温: 開始{_fmt(coolant_start)}°C → 終了{_fmt(coolant_end)}°C
- ECU電圧: 開始{_fmt(ecu_start, 2)}V → 終了{_fmt(ecu_end, 2)}V
  （参考: エンジン始動直後は12V前後、始動後は14V前後に上がるのが正常。この変化自体は異常ではない）

## {BUCKET_SEC}秒バケットごとの時系列データ（CSV、各行は該当バケットの代表座標`lat`/`lon`から始まります）

**重要（時刻ではなく座標で言及すること）**: このCSVには秒数の列がありません。特定の瞬間・区間に
ついて触れる際は、「◯◯秒付近では」のような秒数表現は使わず、**該当する行の`lat`/`lon`を使って
`{{lat:35.7931576,lon:139.5804419}}`のような形式でその瞬間を示してください**（本文中にそのまま
埋め込んでよい）。この座標マーカーは後で地図上の位置に機械的に変換されるための出力なので、
値は該当行の`lat`/`lon`列の数値をそのまま使い、四捨五入や丸めをしないでください。**lat/lonが
空欄の行では座標マーカーを使わず、「GPS未捕捉の時間帯」のように表現してください。**

**重要**: `ltft_pct`/`stft_pct`/`boost_kpa`/`timing_deg`には、この車の過去の実績と比較した
判定済みのラベル（`_level`列）が付いています。数値そのものを再解釈したり独自に「異常」「正常」を
判断し直したりしないでください（それは既に判定済みです）。**ただし、ラベルを単なるタグとして
文中に繰り返すだけでなく、そのラベルが物理的・機械的に何を意味するかを踏まえて分析・説明に
組み込んでください**（例えば「やや薄め」であれば燃料が薄い方向に補正されている状態が一般的に
どういう状況で起きやすいか、といった意味を汲んだ説明）。**ただし「この車の場合は具体的に◯◯が
原因」のようにこの車固有の根本原因を断定するのは避け、一般的に起こりうる背景の説明に留めてください**
（DTC等の診断情報は渡していないため、断定できる根拠がない）。

**重要**: ラベルの意味の説明は、以下で与えた説明の範囲に留めてください。そこから独自に一般知識に
基づく注意喚起を付け加えないでください。レポートは今回のデータから実際に読み取れた事実の報告を
優先し、そこから発展した一般論的な警告・懸念表明は最小限にしてください。

ラベルの意味（この物理的な意味を踏まえて説明すること）:
{_FIELD_LABEL_MEANINGS}
**説明文では、統計用語（偏差値・標準偏差・z-score等）は一切使わず、自然な日本語だけで表現してください。**

**最重要**: ②「注意すべき数値の指摘」は、CSV中に実際に「やや◯◯」または「大きく◯◯」という
判定ラベルが1つ以上存在する場合のみ書いてください。全てのバケット・全ての項目が「通常どおり」
だった場合は、無理に何かを指摘しようとせず、②には「今回のデータでは特筆すべき数値の逸脱は
ありませんでした」と正直に書いてください。存在しないラベルを作り出したり、ある区間の座標と
別の区間・別の項目のラベルを組み合わせて言及したりしないでください。座標マーカーを使う場合は、
必ずその座標と同じ行にあるラベル・数値だけを紐づけてください。

- `ltft_pct_mean`/`ltft_pct_level`: 長期燃料補正(%)の区間平均と判定ラベル
- `stft_pct_mean`/`stft_pct_level`: 短期燃料補正(%)の区間平均と判定ラベル
- `boost_kpa_mean`/`boost_kpa_level`: ブースト圧(kPa、負値は負圧)の区間平均と判定ラベル
- `timing_deg_mean`/`timing_deg_level`: 点火時期(°BTDC)の区間平均と判定ラベル
- `rpm`/`throttle_pct`/`speed_kmh`/`iat_c`/`load_pct`: 判定ラベルなし、各項目に_max/_min/_meanの
  3列のみ（参考情報。`speed_kmh`/`load_pct`は⑤走行の特徴の読み取りに使う）

**⑤走行の特徴について**: `speed_kmh`と`load_pct`の時系列の"形"から、このトリップがどんな走り方
だったかを読み取って記述してください（②のような数値の異常判定ではなく、走行パターンの観察です）。
- `speed_kmh`が高い状態（目安80km/h以上）のバケットが多く連続し、停止に近い状態
  （`speed_kmh`がほぼ0）のバケットがほとんど無ければ、高速道路のような速度域を中心に休憩を
  ほとんど挟まずに走行したと考えられます。逆に低速・停止のバケットが多ければ市街地寄りの
  走行と考えられます
- `speed_kmh`がほぼ一定（加速していない）のに`load_pct`が他の区間より明らかに高いバケットが
  連続していれば、上り坂を継続して走行していた可能性があります。該当する区間があれば
  座標マーカーで示してください
- 実際の道路種別（高速道路か一般道か、坂道かどうか）を示す情報そのものは渡していないため、
  「〜だった」と断定せず「〜のような速度域でした」「〜の可能性があります」という推測の
  言い回しに留めてください
- 該当する特徴的な区間が見当たらない場合は、無理に何かを見つけようとせず
  「特に特徴的な区間は見られませんでした」と正直に書いてください

```csv
{csv_text}
```"""

    return [{"role": "user", "content": [{"text": prompt}]}]


def _invoke_bedrock(messages: list[dict]) -> str:
    resp = bedrock_client.converse(modelId=BEDROCK_MODEL_ID, messages=messages)
    return resp["output"]["message"]["content"][0]["text"]


# AIが「lat/lonが空欄なら座標マーカーを使わない」という指示に従わず、値が空のマーカーを
# 出力することがPOCで確認されている（プロンプト指示だけでは防ぎきれない残課題）ための後段ガード
_EMPTY_COORD_MARKER_RE = re.compile(r"\{lat:\s*,\s*lon:\s*\}")


def _strip_empty_coord_markers(text: str) -> str:
    return _EMPTY_COORD_MARKER_RE.sub("", text)


def _generate_narrative(device_id: str, trip_start: int, trip_end: int, rows: list[dict], summary: dict) -> str:
    """バケット化・ベースライン計算・位置情報取得・Bedrock呼び出しを結線する。ナレーティブは
    補助的機能のため、途中で何が失敗しても例外を握りつぶし空文字を返す
    （トリップ集計自体の保存を失敗させてはならないため）。"""
    try:
        past_trips = _load_trips(device_id)
        trip_baseline = _compute_trip_baseline(past_trips)
        row_baseline = _compute_row_baseline(past_trips)

        buckets = _bucket_rows(rows, trip_start)
        csv_text = _bucket_csv(buckets, row_baseline)

        sorted_rows = sorted(rows, key=lambda r: r["obd_ts"])
        start_desc = _describe_location(sorted_rows[0].get("lat"), sorted_rows[0].get("lon"))
        end_desc = _describe_location(sorted_rows[-1].get("lat"), sorted_rows[-1].get("lon"))

        slow_endpoints = {
            "coolant_c": (summary.get("coolant_start"), summary.get("coolant_end")),
            "ecu_voltage": _clean_field_endpoints(rows, "ecu_voltage"),
        }

        messages = _build_narrative_prompt(summary, start_desc, end_desc, slow_endpoints, trip_baseline, csv_text)
        text = _invoke_bedrock(messages)
        return _strip_empty_coord_markers(text)
    except Exception as e:
        print(f"[trip_analysis] narrative generation failed (device_id={device_id}, trip_start={trip_start}): {e}")
        return ""


def _is_admin(event: dict) -> bool:
    """admin/index.pyと同一ロジック。API GatewayのJWT Authorizerはトークンの真正性のみ検証し
    Cognitoグループ（admin/viewer）までは見ないため、Lambda側でも同じチェックを行う
    （defense in depth。JWT Authorizerはmobile/webどちらのクライアントも通すため必須）。"""
    try:
        groups = event["requestContext"]["authorizer"]["jwt"]["claims"].get("cognito:groups", "")
        return "admin" in groups
    except Exception:
        return False


def handler(event, context):
    """自己呼び出しイベント（trip_analysis_job）かAPI Gateway由来かで分岐する薄いルーター。
    自己呼び出しはHTTPコンテキストを持たないためadminチェックを行わない（内部呼び出しのため）。"""
    if event.get("trip_analysis_job"):
        _process_job(event["job_id"], event["device_id"], event["start_ts"], event["end_ts"], event["started_at"])
        return {}

    if not _is_admin(event):
        return _err(403, "admin only")

    method = event["requestContext"]["http"]["method"]
    path = event.get("rawPath", "")
    if method == "POST" and path.endswith("/narrative"):
        return _handle_regenerate_narrative(event)
    if method == "POST":
        return _handle_start(event)
    if method == "GET":
        return _handle_get(event)
    return _err(405, "method not allowed")
