import itertools
import json
import os
from datetime import datetime, timezone

import boto3
import pytest


def _s3():
    return boto3.client("s3")


def _list_trip_keys(device_id):
    resp = _s3().list_objects_v2(Bucket=os.environ["S3_BUCKET"], Prefix=f"trip-analysis/{device_id}/")
    return sorted(o["Key"] for o in resp.get("Contents", []))


# ---- _split_trips ----


def test_split_trips_empty_returns_empty_list(trip_analysis):
    assert trip_analysis._split_trips([]) == []


def test_split_trips_single_row_returns_one_trip(trip_analysis):
    rows = [{"obd_ts": 100}]
    assert trip_analysis._split_trips(rows, gap_sec=600) == [[{"obd_ts": 100}]]


def test_split_trips_does_not_split_at_exact_gap(trip_analysis):
    rows = [{"obd_ts": 100}, {"obd_ts": 700}]  # 差=600（ちょうどgap_sec）
    result = trip_analysis._split_trips(rows, gap_sec=600)
    assert result == [[{"obd_ts": 100}, {"obd_ts": 700}]]


def test_split_trips_splits_when_gap_exceeded(trip_analysis):
    rows = [{"obd_ts": 100}, {"obd_ts": 701}]  # 差=601（gap_sec超過）
    result = trip_analysis._split_trips(rows, gap_sec=600)
    assert result == [[{"obd_ts": 100}], [{"obd_ts": 701}]]


def test_split_trips_splits_multiple_trips(trip_analysis):
    rows = [
        {"obd_ts": 100}, {"obd_ts": 200},
        {"obd_ts": 900},
        {"obd_ts": 950}, {"obd_ts": 1000},
    ]
    result = trip_analysis._split_trips(rows, gap_sec=600)
    assert result == [
        [{"obd_ts": 100}, {"obd_ts": 200}],
        [{"obd_ts": 900}, {"obd_ts": 950}, {"obd_ts": 1000}],
    ]


def test_split_trips_uses_default_gap_sec(trip_analysis):
    # デフォルトはモジュール定数GAP_TIMEOUT_SEC（テスト環境では600秒）
    rows = [{"obd_ts": 100}, {"obd_ts": 701}]
    result = trip_analysis._split_trips(rows)
    assert result == [[{"obd_ts": 100}], [{"obd_ts": 701}]]


# ---- _compute_summary（trip_sweep/tests/test_index.pyから移植） ----


def test_compute_summary_calculates_distance_fuel_and_stats(trip_analysis):
    rows = [
        {"obd_ts": 100, "lat": 35.0, "lon": 139.0, "fuel_rate_lph": 2.0,
         "ltft_pct": -5.0, "stft_pct": -2.0, "catalyst_temp_c": 400.0, "boost_kpa": 10.0, "coolant_c": 60.0},
        {"obd_ts": 102, "lat": 35.001, "lon": 139.0, "fuel_rate_lph": 4.0,
         "ltft_pct": -7.0, "stft_pct": 2.0, "catalyst_temp_c": 420.0, "boost_kpa": 30.0, "coolant_c": 65.0},
    ]
    summary = trip_analysis._compute_summary(rows)

    assert summary["duration_sec"] == 2
    assert summary["distance_km"] == pytest.approx(0.1112, abs=1e-3)  # 緯度差0.001度 ≈ 111.2m
    assert summary["fuel_l"] == pytest.approx((2.0 + 4.0) / 2 * 2 / 3600, abs=1e-6)
    assert summary["ltft_avg"] == pytest.approx(-6.0)
    assert summary["stft_avg"] == pytest.approx(0.0)
    assert summary["catalyst_temp_max"] == 420.0
    assert summary["boost_kpa_max"] == 30.0
    assert summary["coolant_start"] == 60.0
    assert summary["coolant_end"] == 65.0
    assert summary["fuel_economy_km_l"] == pytest.approx(summary["distance_km"] / summary["fuel_l"])


def test_compute_summary_handles_missing_gps_and_zero_fuel(trip_analysis):
    rows = [
        {"obd_ts": 100, "lat": None, "lon": None, "fuel_rate_lph": 0.0,
         "ltft_pct": None, "stft_pct": None, "catalyst_temp_c": None, "boost_kpa": None, "coolant_c": None},
        {"obd_ts": 110, "lat": None, "lon": None, "fuel_rate_lph": 0.0,
         "ltft_pct": None, "stft_pct": None, "catalyst_temp_c": None, "boost_kpa": None, "coolant_c": None},
    ]
    summary = trip_analysis._compute_summary(rows)

    assert summary["distance_km"] == 0.0
    assert summary["fuel_l"] == 0.0
    assert summary["fuel_economy_km_l"] is None  # fuel_l=0は0除算を避けてNone
    assert summary["ltft_avg"] is None
    assert summary["catalyst_temp_max"] is None


# ---- _partition_filters ----


def test_partition_filters_single_hour(trip_analysis):
    start_dt = datetime(2026, 1, 1, 0, 0, 0, tzinfo=timezone.utc)
    end_dt = datetime(2026, 1, 1, 0, 30, 0, tzinfo=timezone.utc)
    filters = trip_analysis._partition_filters(int(start_dt.timestamp()), int(end_dt.timestamp()))
    assert "year = '2026'" in filters
    assert "month = '01'" in filters
    assert "day = '01'" in filters
    assert "hour = '00'" in filters


def test_partition_filters_spans_multiple_hours(trip_analysis):
    start_dt = datetime(2026, 1, 1, 0, 0, 0, tzinfo=timezone.utc)
    end_dt = datetime(2026, 1, 1, 1, 0, 0, tzinfo=timezone.utc)
    filters = trip_analysis._partition_filters(int(start_dt.timestamp()), int(end_dt.timestamp()))
    hour_filter = next(f for f in filters if f.startswith("hour"))
    assert "'00'" in hour_filter
    assert "'01'" in hour_filter


def test_partition_filters_spans_year_boundary(trip_analysis):
    start_dt = datetime(2025, 12, 31, 23, 0, 0, tzinfo=timezone.utc)
    end_dt = datetime(2026, 1, 1, 1, 0, 0, tzinfo=timezone.utc)
    filters = trip_analysis._partition_filters(int(start_dt.timestamp()), int(end_dt.timestamp()))
    year_filter = next(f for f in filters if f.startswith("year"))
    assert "'2025'" in year_filter
    assert "'2026'" in year_filter


# ---- _parse_athena_results ----


def test_parse_athena_results_single_page(trip_analysis, monkeypatch):
    monkeypatch.setattr(
        trip_analysis.athena,
        "get_query_results",
        lambda **kw: {
            "ResultSet": {
                "ResultSetMetadata": {"ColumnInfo": [{"Label": "obd_ts"}, {"Label": "lat"}]},
                "Rows": [
                    {"Data": [{"VarCharValue": "obd_ts"}, {"VarCharValue": "lat"}]},
                    {"Data": [{"VarCharValue": "100"}, {"VarCharValue": "35.0"}]},
                ],
            }
        },
    )
    rows = trip_analysis._parse_athena_results("eid-1")
    assert rows == [{"obd_ts": 100.0, "lat": 35.0}]


def test_parse_athena_results_paginates_with_next_token(trip_analysis, monkeypatch):
    pages = [
        {
            "ResultSet": {
                "ResultSetMetadata": {"ColumnInfo": [{"Label": "obd_ts"}]},
                "Rows": [
                    {"Data": [{"VarCharValue": "obd_ts"}]},
                    {"Data": [{"VarCharValue": "100"}]},
                ],
            },
            "NextToken": "token-2",
        },
        {
            "ResultSet": {
                "ResultSetMetadata": {"ColumnInfo": [{"Label": "obd_ts"}]},
                "Rows": [
                    {"Data": [{"VarCharValue": "200"}]},
                ],
            },
        },
    ]
    call_count = {"n": 0}

    def _fake_get_query_results(**kw):
        page = pages[call_count["n"]]
        call_count["n"] += 1
        return page

    monkeypatch.setattr(trip_analysis.athena, "get_query_results", _fake_get_query_results)
    rows = trip_analysis._parse_athena_results("eid-1")
    assert rows == [{"obd_ts": 100.0}, {"obd_ts": 200.0}]


def test_parse_athena_results_keeps_non_numeric_as_string(trip_analysis, monkeypatch):
    monkeypatch.setattr(
        trip_analysis.athena,
        "get_query_results",
        lambda **kw: {
            "ResultSet": {
                "ResultSetMetadata": {"ColumnInfo": [{"Label": "device_id"}]},
                "Rows": [
                    {"Data": [{"VarCharValue": "device_id"}]},
                    {"Data": [{"VarCharValue": "car-iot-abc"}]},
                ],
            }
        },
    )
    rows = trip_analysis._parse_athena_results("eid-1")
    assert rows == [{"device_id": "car-iot-abc"}]


# ---- _run_athena_query ----


def test_run_athena_query_returns_parsed_results_on_success(trip_analysis, monkeypatch):
    monkeypatch.setattr(trip_analysis.athena, "start_query_execution", lambda **kw: {"QueryExecutionId": "eid-1"})
    monkeypatch.setattr(
        trip_analysis.athena,
        "get_query_execution",
        lambda **kw: {"QueryExecution": {"Status": {"State": "SUCCEEDED"}}},
    )
    monkeypatch.setattr(
        trip_analysis.athena,
        "get_query_results",
        lambda **kw: {
            "ResultSet": {
                "ResultSetMetadata": {"ColumnInfo": [{"Label": "obd_ts"}]},
                "Rows": [
                    {"Data": [{"VarCharValue": "obd_ts"}]},
                    {"Data": [{"VarCharValue": "100"}]},
                ],
            }
        },
    )
    rows = trip_analysis._run_athena_query("SELECT 1")
    assert rows == [{"obd_ts": 100.0}]


def test_run_athena_query_polls_until_succeeded(trip_analysis, monkeypatch):
    monkeypatch.setattr(trip_analysis.athena, "start_query_execution", lambda **kw: {"QueryExecutionId": "eid-1"})
    states = iter(["RUNNING", "RUNNING", "SUCCEEDED"])
    monkeypatch.setattr(
        trip_analysis.athena,
        "get_query_execution",
        lambda **kw: {"QueryExecution": {"Status": {"State": next(states)}}},
    )
    monkeypatch.setattr(
        trip_analysis.athena,
        "get_query_results",
        lambda **kw: {
            "ResultSet": {
                "ResultSetMetadata": {"ColumnInfo": [{"Label": "obd_ts"}]},
                "Rows": [{"Data": [{"VarCharValue": "obd_ts"}]}],
            }
        },
    )
    monkeypatch.setattr(trip_analysis.time, "sleep", lambda s: None)  # 高速化
    rows = trip_analysis._run_athena_query("SELECT 1")
    assert rows == []


def test_run_athena_query_raises_on_failed(trip_analysis, monkeypatch):
    monkeypatch.setattr(trip_analysis.athena, "start_query_execution", lambda **kw: {"QueryExecutionId": "eid-1"})
    monkeypatch.setattr(
        trip_analysis.athena,
        "get_query_execution",
        lambda **kw: {"QueryExecution": {"Status": {"State": "FAILED", "StateChangeReason": "syntax error"}}},
    )
    with pytest.raises(RuntimeError, match="syntax error"):
        trip_analysis._run_athena_query("SELECT 1")


def test_run_athena_query_raises_timeout_error(trip_analysis, monkeypatch):
    monkeypatch.setattr(trip_analysis.athena, "start_query_execution", lambda **kw: {"QueryExecutionId": "eid-1"})
    monkeypatch.setattr(
        trip_analysis.athena,
        "get_query_execution",
        lambda **kw: {"QueryExecution": {"Status": {"State": "RUNNING"}}},
    )
    monkeypatch.setattr(trip_analysis.time, "sleep", lambda s: None)
    # 呼ばれるたびに大きく進む時刻を返し、必ずdeadlineを超過させる
    times = itertools.count(1000, trip_analysis.ATHENA_POLL_TIMEOUT_SEC + 100)
    monkeypatch.setattr(trip_analysis.time, "time", lambda: next(times))
    with pytest.raises(TimeoutError):
        trip_analysis._run_athena_query("SELECT 1")


# ---- _save_trip ----


def test_save_trip_writes_object_with_seq_001(trip_analysis):
    trip_analysis._save_trip("dev1", 1700000000, 1700001800, {"distance_km": 1.5}, row_count=100)

    keys = _list_trip_keys("dev1")
    assert len(keys) == 1
    assert keys[0].endswith("_v01_001.json")
    assert "1700000000_1700001800" in keys[0]


def test_save_trip_increments_seq_on_repeat(trip_analysis):
    trip_analysis._save_trip("dev1", 1700000000, 1700001800, {"distance_km": 1.5}, row_count=100)
    trip_analysis._save_trip("dev1", 1700000000, 1700001800, {"distance_km": 1.5}, row_count=100)

    keys = _list_trip_keys("dev1")
    assert len(keys) == 2
    assert keys[0].endswith("_001.json")
    assert keys[1].endswith("_002.json")


def test_save_trip_content_has_expected_fields(trip_analysis):
    trip_analysis._save_trip(
        "dev1", 1700000000, 1700001800,
        {"distance_km": 1.5, "fuel_economy_km_l": None}, row_count=50,
    )

    key = _list_trip_keys("dev1")[0]
    obj = _s3().get_object(Bucket=os.environ["S3_BUCKET"], Key=key)
    item = json.loads(obj["Body"].read())
    assert item["device_id"] == "dev1"
    assert item["session_start"] == 1700000000
    assert item["session_end"] == 1700001800
    assert item["row_count"] == 50
    assert item["distance_km"] == 1.5
    assert "fuel_economy_km_l" not in item  # Noneのキーは書き込まない
    assert item["narrative"] == ""


def test_save_trip_key_is_partitioned_by_session_end_year_month(trip_analysis):
    end_dt = datetime(2026, 3, 15, 12, 0, 0, tzinfo=timezone.utc)
    trip_analysis._save_trip("dev1", int(end_dt.timestamp()) - 100, int(end_dt.timestamp()), {}, row_count=1)

    keys = _list_trip_keys("dev1")
    assert keys[0].startswith("trip-analysis/dev1/year=2026/month=03/")


# ---- _latest_session_end ----


def test_latest_session_end_returns_none_when_no_trips(trip_analysis):
    assert trip_analysis._latest_session_end("dev1") is None


def test_latest_session_end_returns_the_only_trip(trip_analysis):
    trip_analysis._save_trip("dev1", 1700000000, 1700001800, {}, row_count=1)
    assert trip_analysis._latest_session_end("dev1") == 1700001800


def test_latest_session_end_picks_latest_across_months(trip_analysis):
    jan = datetime(2026, 1, 15, 0, 0, 0, tzinfo=timezone.utc)
    mar = datetime(2026, 3, 15, 0, 0, 0, tzinfo=timezone.utc)
    trip_analysis._save_trip("dev1", int(jan.timestamp()) - 100, int(jan.timestamp()), {}, row_count=1)
    trip_analysis._save_trip("dev1", int(mar.timestamp()) - 100, int(mar.timestamp()), {}, row_count=1)

    assert trip_analysis._latest_session_end("dev1") == int(mar.timestamp())


def test_latest_session_end_unaffected_by_repeated_seq(trip_analysis):
    trip_analysis._save_trip("dev1", 1700000000, 1700001800, {}, row_count=1)
    trip_analysis._save_trip("dev1", 1700000000, 1700001800, {}, row_count=1)  # seq=002になる

    assert trip_analysis._latest_session_end("dev1") == 1700001800


def test_latest_session_end_ignores_other_devices(trip_analysis):
    trip_analysis._save_trip("dev1", 1700000000, 1700001800, {}, row_count=1)
    trip_analysis._save_trip("dev2", 1800000000, 1800001800, {}, row_count=1)

    assert trip_analysis._latest_session_end("dev1") == 1700001800


# ---- _earliest_obd_ts ----


def _put_obd_object(dt: datetime, device_id="dev1"):
    key = f"obd/year={dt.year:04d}/month={dt.month:02d}/day={dt.day:02d}/hour={dt.hour:02d}/{device_id}-x.ndjson"
    _s3().put_object(Bucket=os.environ["S3_BUCKET"], Key=key, Body=b"{}")


def test_earliest_obd_ts_returns_none_when_no_data(trip_analysis):
    assert trip_analysis._earliest_obd_ts() is None


def test_earliest_obd_ts_returns_earliest_partition(trip_analysis):
    _put_obd_object(datetime(2026, 3, 15, 10, tzinfo=timezone.utc))
    _put_obd_object(datetime(2026, 1, 5, 3, tzinfo=timezone.utc))
    _put_obd_object(datetime(2026, 2, 20, 18, tzinfo=timezone.utc))

    ts = trip_analysis._earliest_obd_ts()
    expected = int(datetime(2026, 1, 5, 3, tzinfo=timezone.utc).timestamp())
    assert ts == expected


# ---- _write_job_status / _read_job_status / _find_recent_running_job ----


def test_write_and_read_job_status_roundtrip(trip_analysis):
    trip_analysis._write_job_status("dev1", "job-1", 1000, {"job_id": "job-1", "status": "RUNNING"})
    status = trip_analysis._read_job_status("dev1", "job-1")
    assert status == {"job_id": "job-1", "status": "RUNNING"}


def test_read_job_status_returns_none_when_not_found(trip_analysis):
    assert trip_analysis._read_job_status("dev1", "missing") is None


def test_find_recent_running_job_returns_job_within_guard_window(trip_analysis, monkeypatch):
    trip_analysis._write_job_status(
        "dev1", "job-1", 1000, {"job_id": "job-1", "status": "RUNNING", "started_at": 1000}
    )
    monkeypatch.setattr(trip_analysis.time, "time", lambda: 1000 + 100)  # ガード時間未満経過
    job = trip_analysis._find_recent_running_job("dev1")
    assert job["job_id"] == "job-1"


def test_find_recent_running_job_returns_none_after_guard_window(trip_analysis, monkeypatch):
    trip_analysis._write_job_status(
        "dev1", "job-1", 1000, {"job_id": "job-1", "status": "RUNNING", "started_at": 1000}
    )
    monkeypatch.setattr(trip_analysis.time, "time", lambda: 1000 + trip_analysis.RECENT_JOB_GUARD_SEC + 1)
    job = trip_analysis._find_recent_running_job("dev1")
    assert job is None  # 自己修復: 新規ジョブの起動を許可


def test_find_recent_running_job_returns_none_when_succeeded(trip_analysis, monkeypatch):
    trip_analysis._write_job_status(
        "dev1", "job-1", 1000, {"job_id": "job-1", "status": "SUCCEEDED", "started_at": 1000}
    )
    monkeypatch.setattr(trip_analysis.time, "time", lambda: 1000 + 10)
    job = trip_analysis._find_recent_running_job("dev1")
    assert job is None


def test_find_recent_running_job_returns_none_when_no_jobs(trip_analysis):
    assert trip_analysis._find_recent_running_job("dev1") is None


# ---- _load_trips ----


def test_load_trips_returns_empty_list_when_no_trips(trip_analysis):
    assert trip_analysis._load_trips("dev1") == []


def test_load_trips_returns_trip_contents(trip_analysis):
    trip_analysis._save_trip("dev1", 1700000000, 1700001800, {"distance_km": 1.0}, row_count=10)

    trips = trip_analysis._load_trips("dev1")

    assert len(trips) == 1
    assert trips[0]["session_start"] == 1700000000
    assert trips[0]["distance_km"] == 1.0


def test_load_trips_returns_newest_first(trip_analysis):
    jan = datetime(2026, 1, 15, tzinfo=timezone.utc)
    mar = datetime(2026, 3, 15, tzinfo=timezone.utc)
    trip_analysis._save_trip("dev1", int(jan.timestamp()) - 100, int(jan.timestamp()), {}, row_count=1)
    trip_analysis._save_trip("dev1", int(mar.timestamp()) - 100, int(mar.timestamp()), {}, row_count=1)

    trips = trip_analysis._load_trips("dev1")

    assert trips[0]["session_start"] == int(mar.timestamp()) - 100
    assert trips[1]["session_start"] == int(jan.timestamp()) - 100


def test_load_trips_respects_limit(trip_analysis):
    for i in range(5):
        base = 1700000000 + i * 10000
        trip_analysis._save_trip("dev1", base, base + 100, {}, row_count=1)

    trips = trip_analysis._load_trips("dev1", limit=2)

    assert len(trips) == 2


def test_load_trips_ignores_other_devices(trip_analysis):
    trip_analysis._save_trip("dev1", 1700000000, 1700001800, {}, row_count=1)
    trip_analysis._save_trip("dev2", 1800000000, 1800001800, {}, row_count=1)

    trips = trip_analysis._load_trips("dev1")

    assert len(trips) == 1


# ---- _handle_start ----


def test_handle_start_rejects_invalid_device_id(trip_analysis):
    event = {"body": json.dumps({"device_id": "invalid id with space"})}
    resp = trip_analysis._handle_start(event)
    assert resp["statusCode"] == 400


def test_handle_start_returns_running_job_when_duplicate(trip_analysis, monkeypatch):
    trip_analysis._write_job_status(
        "dev1", "job-1", 1000, {"job_id": "job-1", "status": "RUNNING", "started_at": 1000}
    )
    monkeypatch.setattr(trip_analysis.time, "time", lambda: 1000 + 100)
    monkeypatch.setattr(
        trip_analysis.lambda_client, "invoke", lambda **kw: pytest.fail("should not invoke a new job")
    )

    event = {"body": json.dumps({"device_id": "dev1"})}
    resp = trip_analysis._handle_start(event)

    body = json.loads(resp["body"])
    assert body["job_id"] == "job-1"


def test_handle_start_first_time_uses_earliest_obd_ts(trip_analysis, monkeypatch):
    _put_obd_object(datetime(2026, 1, 1, 0, tzinfo=timezone.utc))
    invoked = {}
    monkeypatch.setattr(trip_analysis.lambda_client, "invoke", lambda **kw: invoked.update(kw))
    now_ts = int(datetime(2026, 1, 2, 0, tzinfo=timezone.utc).timestamp())
    monkeypatch.setattr(trip_analysis.time, "time", lambda: now_ts)

    event = {"body": json.dumps({"device_id": "dev1"})}
    resp = trip_analysis._handle_start(event)

    body = json.loads(resp["body"])
    assert body["status"] == "RUNNING"
    assert body["range"]["start_ts"] == int(datetime(2026, 1, 1, 0, tzinfo=timezone.utc).timestamp())
    payload = json.loads(invoked["Payload"])
    assert payload["trip_analysis_job"] is True
    assert payload["device_id"] == "dev1"


def test_handle_start_second_time_uses_latest_session_end(trip_analysis, monkeypatch):
    trip_analysis._save_trip("dev1", 1700000000, 1700001800, {}, row_count=1)
    monkeypatch.setattr(trip_analysis.lambda_client, "invoke", lambda **kw: None)
    monkeypatch.setattr(trip_analysis.time, "time", lambda: 1700100000)

    event = {"body": json.dumps({"device_id": "dev1"})}
    resp = trip_analysis._handle_start(event)

    body = json.loads(resp["body"])
    assert body["range"]["start_ts"] == 1700001801


def test_handle_start_clamps_to_max_window(trip_analysis, monkeypatch):
    trip_analysis._save_trip("dev1", 1700000000, 1700001800, {}, row_count=1)
    monkeypatch.setattr(trip_analysis.lambda_client, "invoke", lambda **kw: None)
    far_future = 1700001800 + trip_analysis.MAX_WINDOW_HOURS * 3600 * 10
    monkeypatch.setattr(trip_analysis.time, "time", lambda: far_future)

    event = {"body": json.dumps({"device_id": "dev1"})}
    resp = trip_analysis._handle_start(event)

    body = json.loads(resp["body"])
    expected_end = 1700001801 + trip_analysis.MAX_WINDOW_HOURS * 3600
    assert body["range"]["end_ts"] == expected_end
    assert body["has_more"] is True


def test_handle_start_no_new_data_returns_succeeded_immediately(trip_analysis, monkeypatch):
    trip_analysis._save_trip("dev1", 1700000000, 1700001800, {}, row_count=1)
    monkeypatch.setattr(trip_analysis.time, "time", lambda: 1700001800)  # last_end+1 >= now

    event = {"body": json.dumps({"device_id": "dev1"})}
    resp = trip_analysis._handle_start(event)

    body = json.loads(resp["body"])
    assert body["status"] == "SUCCEEDED"
    assert body["trips_saved"] == 0


# ---- _process_job ----


def _row(obd_ts, **kw):
    row = {
        "obd_ts": obd_ts,
        "lat": 35.0,
        "lon": 139.0,
        "fuel_rate_lph": 1.0,
        "ltft_pct": 0.0,
        "stft_pct": 0.0,
        "catalyst_temp_c": 500.0,
        "boost_kpa": 100.0,
        "coolant_c": 80.0,
    }
    row.update(kw)
    return row


def test_process_job_saves_settled_trips_and_marks_succeeded(trip_analysis, monkeypatch):
    # 2トリップ分（gap=600超過で分割）、どちらもnowから見て十分過去（settled）
    rows = [_row(1000), _row(1060), _row(2000), _row(2060)]
    monkeypatch.setattr(trip_analysis, "_query_obd_data", lambda device_id, start_ts, end_ts: rows)
    monkeypatch.setattr(trip_analysis.time, "time", lambda: 2060 + trip_analysis.GAP_TIMEOUT_SEC + 1)

    trip_analysis._write_job_status(
        "dev1", "job-1", 900,
        {"job_id": "job-1", "device_id": "dev1", "status": "RUNNING", "started_at": 900,
         "range": {"start_ts": 1000, "end_ts": 3000}, "trips_saved": 0, "has_more": False, "error": None},
    )

    trip_analysis._process_job("job-1", "dev1", 1000, 3000, 900)

    status = trip_analysis._read_job_status("dev1", "job-1")
    assert status["status"] == "SUCCEEDED"
    assert status["trips_saved"] == 2
    assert status["range"] == {"start_ts": 1000, "end_ts": 3000}  # 既存フィールドを保持
    assert len(_list_trip_keys("dev1")) == 2


def test_process_job_holds_back_unsettled_last_trip(trip_analysis, monkeypatch):
    rows = [_row(1000), _row(1060), _row(2000), _row(2060)]
    monkeypatch.setattr(trip_analysis, "_query_obd_data", lambda device_id, start_ts, end_ts: rows)
    # 2つ目のトリップはnowから見て直近（gap未経過）→保存されない
    monkeypatch.setattr(trip_analysis.time, "time", lambda: 2060 + 10)

    trip_analysis._write_job_status(
        "dev1", "job-1", 900,
        {"job_id": "job-1", "device_id": "dev1", "status": "RUNNING", "started_at": 900,
         "range": {"start_ts": 1000, "end_ts": 3000}, "trips_saved": 0, "has_more": False, "error": None},
    )

    trip_analysis._process_job("job-1", "dev1", 1000, 3000, 900)

    status = trip_analysis._read_job_status("dev1", "job-1")
    assert status["status"] == "SUCCEEDED"
    assert status["trips_saved"] == 1
    assert len(_list_trip_keys("dev1")) == 1


def test_process_job_discards_short_trips_as_noise(trip_analysis, monkeypatch):
    # 継続時間=5秒（MIN_TRIP_DURATION_SEC=30未満）のトリップのみ
    rows = [_row(1000), _row(1005)]
    monkeypatch.setattr(trip_analysis, "_query_obd_data", lambda device_id, start_ts, end_ts: rows)
    monkeypatch.setattr(trip_analysis.time, "time", lambda: 1005 + trip_analysis.GAP_TIMEOUT_SEC + 1)

    trip_analysis._write_job_status(
        "dev1", "job-1", 900,
        {"job_id": "job-1", "device_id": "dev1", "status": "RUNNING", "started_at": 900,
         "range": {"start_ts": 1000, "end_ts": 3000}, "trips_saved": 0, "has_more": False, "error": None},
    )

    trip_analysis._process_job("job-1", "dev1", 1000, 3000, 900)

    status = trip_analysis._read_job_status("dev1", "job-1")
    assert status["status"] == "SUCCEEDED"
    assert status["trips_saved"] == 0
    assert _list_trip_keys("dev1") == []


def test_process_job_marks_failed_on_athena_error(trip_analysis, monkeypatch):
    def _raise(device_id, start_ts, end_ts):
        raise RuntimeError("athena boom")

    monkeypatch.setattr(trip_analysis, "_query_obd_data", _raise)

    trip_analysis._write_job_status(
        "dev1", "job-1", 900,
        {"job_id": "job-1", "device_id": "dev1", "status": "RUNNING", "started_at": 900,
         "range": {"start_ts": 1000, "end_ts": 3000}, "trips_saved": 0, "has_more": False, "error": None},
    )

    trip_analysis._process_job("job-1", "dev1", 1000, 3000, 900)

    status = trip_analysis._read_job_status("dev1", "job-1")
    assert status["status"] == "FAILED"
    assert "athena boom" in status["error"]


def test_process_job_handles_no_rows(trip_analysis, monkeypatch):
    monkeypatch.setattr(trip_analysis, "_query_obd_data", lambda device_id, start_ts, end_ts: [])

    trip_analysis._write_job_status(
        "dev1", "job-1", 900,
        {"job_id": "job-1", "device_id": "dev1", "status": "RUNNING", "started_at": 900,
         "range": {"start_ts": 1000, "end_ts": 3000}, "trips_saved": 0, "has_more": False, "error": None},
    )

    trip_analysis._process_job("job-1", "dev1", 1000, 3000, 900)

    status = trip_analysis._read_job_status("dev1", "job-1")
    assert status["status"] == "SUCCEEDED"
    assert status["trips_saved"] == 0


# ---- _handle_get ----


def test_handle_get_rejects_invalid_device_id(trip_analysis):
    event = {"queryStringParameters": {"device_id": "invalid id"}}
    resp = trip_analysis._handle_get(event)
    assert resp["statusCode"] == 400


def test_handle_get_job_returns_404_when_not_found(trip_analysis):
    event = {"queryStringParameters": {"device_id": "dev1", "job_id": "no-such-job"}}
    resp = trip_analysis._handle_get(event)
    assert resp["statusCode"] == 404


def test_handle_get_job_returns_running_status(trip_analysis):
    trip_analysis._write_job_status(
        "dev1", "job-1", 900,
        {"job_id": "job-1", "device_id": "dev1", "status": "RUNNING", "started_at": 900},
    )
    event = {"queryStringParameters": {"device_id": "dev1", "job_id": "job-1"}}
    resp = trip_analysis._handle_get(event)

    body = json.loads(resp["body"])
    assert resp["statusCode"] == 200
    assert body["status"] == "RUNNING"


def test_handle_get_job_returns_succeeded_status(trip_analysis):
    trip_analysis._write_job_status(
        "dev1", "job-1", 900,
        {"job_id": "job-1", "device_id": "dev1", "status": "SUCCEEDED", "started_at": 900, "trips_saved": 3},
    )
    event = {"queryStringParameters": {"device_id": "dev1", "job_id": "job-1"}}
    resp = trip_analysis._handle_get(event)

    body = json.loads(resp["body"])
    assert body["status"] == "SUCCEEDED"
    assert body["trips_saved"] == 3


def test_handle_get_without_job_id_returns_trip_list(trip_analysis):
    trip_analysis._save_trip("dev1", 1700000000, 1700001800, {}, row_count=1)
    trip_analysis._save_trip("dev1", 1700100000, 1700101800, {}, row_count=1)
    trip_analysis._save_trip("dev2", 1800000000, 1800001800, {}, row_count=1)

    event = {"queryStringParameters": {"device_id": "dev1"}}
    resp = trip_analysis._handle_get(event)

    body = json.loads(resp["body"])
    assert resp["statusCode"] == 200
    assert len(body["trips"]) == 2


# ---- handler ----


def test_handler_routes_self_invoke_to_process_job(trip_analysis, monkeypatch):
    called = {}

    def _fake_process_job(job_id, device_id, start_ts, end_ts, started_at):
        called["job_id"] = job_id
        called["device_id"] = device_id
        called["start_ts"] = start_ts
        called["end_ts"] = end_ts
        called["started_at"] = started_at

    monkeypatch.setattr(trip_analysis, "_process_job", _fake_process_job)

    event = {
        "trip_analysis_job": True, "job_id": "job-1", "device_id": "dev1",
        "start_ts": 1000, "end_ts": 2000, "started_at": 900,
    }
    trip_analysis.handler(event, None)

    assert called == {"job_id": "job-1", "device_id": "dev1", "start_ts": 1000, "end_ts": 2000, "started_at": 900}


def test_handler_routes_post_to_handle_start(trip_analysis, monkeypatch):
    monkeypatch.setattr(trip_analysis, "_handle_start", lambda event: {"statusCode": 200, "body": "start"})

    event = {
        "requestContext": {
            "http": {"method": "POST"},
            "authorizer": {"jwt": {"claims": {"cognito:groups": "[admin]"}}},
        },
        "body": json.dumps({"device_id": "dev1"}),
    }
    resp = trip_analysis.handler(event, None)

    assert resp["body"] == "start"


def test_handler_routes_get_to_handle_get(trip_analysis, monkeypatch):
    monkeypatch.setattr(trip_analysis, "_handle_get", lambda event: {"statusCode": 200, "body": "get"})

    event = {
        "requestContext": {
            "http": {"method": "GET"},
            "authorizer": {"jwt": {"claims": {"cognito:groups": "[admin]"}}},
        },
        "queryStringParameters": {"device_id": "dev1"},
    }
    resp = trip_analysis.handler(event, None)

    assert resp["body"] == "get"


def test_handler_rejects_unknown_method(trip_analysis):
    event = {
        "requestContext": {
            "http": {"method": "DELETE"},
            "authorizer": {"jwt": {"claims": {"cognito:groups": "[admin]"}}},
        }
    }
    resp = trip_analysis.handler(event, None)

    assert resp["statusCode"] == 405


def test_handler_rejects_non_admin(trip_analysis):
    event = {
        "requestContext": {
            "http": {"method": "GET"},
            "authorizer": {"jwt": {"claims": {"cognito:groups": "[viewer]"}}},
        },
        "queryStringParameters": {"device_id": "dev1"},
    }
    resp = trip_analysis.handler(event, None)

    assert resp["statusCode"] == 403


def test_handler_allows_admin(trip_analysis, monkeypatch):
    monkeypatch.setattr(trip_analysis, "_handle_get", lambda event: {"statusCode": 200, "body": "get"})
    event = {
        "requestContext": {
            "http": {"method": "GET"},
            "authorizer": {"jwt": {"claims": {"cognito:groups": "[admin]"}}},
        },
        "queryStringParameters": {"device_id": "dev1"},
    }
    resp = trip_analysis.handler(event, None)

    assert resp["statusCode"] == 200


def test_handler_self_invoke_skips_admin_check(trip_analysis, monkeypatch):
    monkeypatch.setattr(trip_analysis, "_process_job", lambda *a, **kw: None)
    event = {
        "trip_analysis_job": True, "job_id": "job-1", "device_id": "dev1",
        "start_ts": 1000, "end_ts": 2000, "started_at": 900,
    }
    resp = trip_analysis.handler(event, None)

    assert resp == {}
