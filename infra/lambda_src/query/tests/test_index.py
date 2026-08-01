import json
from datetime import datetime, timezone

import boto3
import pytest


def _admin_event(**overrides):
    event = {"queryStringParameters": {}}
    event.update(overrides)
    return event


def _put(bucket, key, body):
    boto3.client("s3").put_object(Bucket=bucket, Key=key, Body=body.encode("utf-8"))


class _FrozenDateTime(datetime):
    @classmethod
    def now(cls, tz=None):
        return datetime(2026, 4, 1, 12, 0, tzinfo=tz)


# ---- _partition_filters_range ----


def test_partition_filters_range_includes_hour_within_72h(query):
    start = datetime(2026, 3, 1, 0, tzinfo=timezone.utc)
    end = datetime(2026, 3, 1, 2, tzinfo=timezone.utc)
    filters = query._partition_filters_range(start, end)
    assert any(f.startswith("hour") for f in filters)


def test_partition_filters_range_omits_hour_beyond_72h(query):
    start = datetime(2026, 3, 1, 0, tzinfo=timezone.utc)
    end = start.replace(day=10)
    filters = query._partition_filters_range(start, end)
    assert not any(f.startswith("hour") for f in filters)


# ---- _validate_inputs ----


@pytest.mark.parametrize(
    "kwargs",
    [
        dict(sensor_type="not-a-type"),
        dict(device_id="bad id!"),
        dict(addr="not-a-mac"),
        dict(id_filter="bad id!"),
    ],
)
def test_validate_inputs_rejects_bad_format(query, kwargs):
    start = datetime(2026, 3, 1, 0, tzinfo=timezone.utc)
    end = datetime(2026, 3, 1, 1, tzinfo=timezone.utc)
    base = dict(sensor_type=None, device_id=None, addr=None, id_filter=None)
    base.update(kwargs)
    with pytest.raises(ValueError):
        query._validate_inputs(base["sensor_type"], base["device_id"], start, end, base["addr"], base["id_filter"])


def test_validate_inputs_rejects_end_before_start(query):
    start = datetime(2026, 3, 1, 1, tzinfo=timezone.utc)
    end = datetime(2026, 3, 1, 0, tzinfo=timezone.utc)
    with pytest.raises(ValueError):
        query._validate_inputs(None, None, start, end, None, None)


def test_validate_inputs_rejects_span_over_720h(query):
    start = datetime(2026, 1, 1, tzinfo=timezone.utc)
    end = datetime(2026, 3, 1, tzinfo=timezone.utc)
    with pytest.raises(ValueError):
        query._validate_inputs(None, None, start, end, None, None)


def test_validate_inputs_accepts_valid(query):
    start = datetime(2026, 3, 1, 0, tzinfo=timezone.utc)
    end = datetime(2026, 3, 1, 1, tzinfo=timezone.utc)
    query._validate_inputs("battery", "dev1", start, end, None, "voltage_1")  # raises nothing


# ---- _select_list / _build_query ----


def test_select_list_nulls_out_missing_columns(query):
    select = query._select_list("battery")
    assert "main" in select
    assert "NULL AS co2" in select
    assert '"$path" AS s3_key' in select


def test_build_query_single_type_has_no_union(query):
    start = datetime(2026, 3, 1, 0, tzinfo=timezone.utc)
    end = datetime(2026, 3, 1, 1, tzinfo=timezone.utc)
    sql = query._build_query("battery", None, start, end, None, None)
    assert "UNION ALL" not in sql
    assert "type = 'battery'" in sql


def test_build_query_no_type_unions_all_types(query):
    start = datetime(2026, 3, 1, 0, tzinfo=timezone.utc)
    end = datetime(2026, 3, 1, 1, tzinfo=timezone.utc)
    sql = query._build_query(None, None, start, end, None, None)
    assert sql.count("UNION ALL") == len(query._TYPE_OWN_COLS) - 1


def test_build_query_device_id_filter_applied_to_all_subqueries(query):
    start = datetime(2026, 3, 1, 0, tzinfo=timezone.utc)
    end = datetime(2026, 3, 1, 1, tzinfo=timezone.utc)
    sql = query._build_query(None, "dev1", start, end, None, None)
    assert sql.count("device_id = 'dev1'") == len(query._TYPE_OWN_COLS)


def test_build_query_addr_filter_only_on_addr_eligible_types(query):
    start = datetime(2026, 3, 1, 0, tzinfo=timezone.utc)
    end = datetime(2026, 3, 1, 1, tzinfo=timezone.utc)
    sql = query._build_query(None, None, start, end, "AA:BB:CC:DD:EE:FF", None)
    assert sql.count("LOWER(addr) = LOWER('AA:BB:CC:DD:EE:FF')") == len(query._ADDR_FILTER_TYPES)


def test_build_query_id_filter_only_on_battery(query):
    start = datetime(2026, 3, 1, 0, tzinfo=timezone.utc)
    end = datetime(2026, 3, 1, 1, tzinfo=timezone.utc)
    sql = query._build_query(None, None, start, end, None, "voltage_1")
    assert sql.count("LOWER(id) = LOWER('voltage_1')") == 1


def test_build_query_orders_by_ts(query):
    start = datetime(2026, 3, 1, 0, tzinfo=timezone.utc)
    end = datetime(2026, 3, 1, 1, tzinfo=timezone.utc)
    sql = query._build_query("battery", None, start, end, None, None)
    assert sql.strip().endswith("ORDER BY ts ASC")


# ---- _downsample ----


def test_downsample_keeps_series_under_limit(query):
    rows = [{"device_id": "d1", "type": "battery", "ts": f"t{i}"} for i in range(5)]
    result = query._downsample(rows)
    assert len(result) == 5


def test_downsample_thins_large_series(query):
    rows = [{"device_id": "d1", "type": "battery", "ts": f"{i:06d}"} for i in range(5000)]
    result = query._downsample(rows)
    assert len(result) == query._MAX_POINTS_PER_SERIES


def test_downsample_sorts_descending_by_ts(query):
    rows = [
        {"device_id": "d1", "type": "battery", "ts": "2026-03-01T10:00:00Z"},
        {"device_id": "d1", "type": "battery", "ts": "2026-03-01T12:00:00Z"},
        {"device_id": "d1", "type": "battery", "ts": "2026-03-01T11:00:00Z"},
    ]
    result = query._downsample(rows)
    assert [r["ts"] for r in result] == [
        "2026-03-01T12:00:00Z",
        "2026-03-01T11:00:00Z",
        "2026-03-01T10:00:00Z",
    ]


def test_downsample_keeps_series_independent(query):
    rows = [{"device_id": "d1", "type": "battery", "ts": "1"}] * 3000 + [
        {"device_id": "d2", "type": "battery", "ts": "1"}
    ]
    result = query._downsample(rows)
    d2_count = sum(1 for r in result if r["device_id"] == "d2")
    assert d2_count == 1


# ---- _parse_results ----


def test_parse_results_converts_numeric_values(query, monkeypatch):
    col_labels = ["ts", "type", "main"]
    resp = {
        "ResultSet": {
            "Rows": [
                {"Data": [{"VarCharValue": c} for c in col_labels]},
                {"Data": [{"VarCharValue": "2026-03-01T10:00:00Z"}, {"VarCharValue": "battery"}, {"VarCharValue": "12.34"}]},
            ],
            "ResultSetMetadata": {"ColumnInfo": [{"Label": c} for c in col_labels]},
        }
    }
    monkeypatch.setattr(query.athena, "get_query_results", lambda **kw: resp)

    rows = query._parse_results("eid1")

    assert rows == [{"ts": "2026-03-01T10:00:00Z", "type": "battery", "main": 12.34}]


def test_parse_results_follows_next_token(query, monkeypatch):
    col_labels = ["ts"]
    page1 = {
        "ResultSet": {
            "Rows": [{"Data": [{"VarCharValue": "ts"}]}, {"Data": [{"VarCharValue": "2026-03-01T10:00:00Z"}]}],
            "ResultSetMetadata": {"ColumnInfo": [{"Label": c} for c in col_labels]},
        },
        "NextToken": "abc",
    }
    page2 = {
        "ResultSet": {
            "Rows": [{"Data": [{"VarCharValue": "2026-03-01T11:00:00Z"}]}],
            "ResultSetMetadata": {"ColumnInfo": [{"Label": c} for c in col_labels]},
        },
    }
    calls = []

    def fake(**kw):
        calls.append(kw)
        return page2 if "NextToken" in kw else page1

    monkeypatch.setattr(query.athena, "get_query_results", fake)

    rows = query._parse_results("eid1")

    assert len(rows) == 2
    assert calls[1]["NextToken"] == "abc"


# ---- _get_labels ----


def test_get_labels_returns_saved_labels(query):
    _put(query.S3_BUCKET, "labels/user-sub-1.json", json.dumps({"dev1": "リビング"}))
    assert query._get_labels("user-sub-1") == {"dev1": "リビング"}


def test_get_labels_missing_key_returns_empty(query):
    assert query._get_labels("no-such-user") == {}


# ---- _get_data_range ----


def test_get_data_range_empty_bucket(query):
    resp = query._get_data_range()
    assert json.loads(resp["body"]) == {"min_ts": None, "max_ts": None}


def test_get_data_range_finds_earliest_and_latest(query, monkeypatch):
    monkeypatch.setattr(query, "datetime", _FrozenDateTime)
    _put(query.S3_BUCKET, "raw/year=2026/month=03/day=01/hour=05/dev-a.json", "{}")
    _put(query.S3_BUCKET, "raw/year=2026/month=03/day=03/hour=09/dev-b.json", "{}")

    resp = query._get_data_range()
    body = json.loads(resp["body"])

    assert body["min_ts"] == "2026-03-01T05:00:00Z"
    assert body["max_ts"] == "2026-03-03T09:59:59Z"


def test_get_data_range_caps_max_ts_at_now(query, monkeypatch):
    monkeypatch.setattr(query, "datetime", _FrozenDateTime)
    _put(query.S3_BUCKET, "raw/year=2026/month=03/day=01/hour=05/dev-a.json", "{}")
    _put(query.S3_BUCKET, "raw/year=2099/month=01/day=01/hour=00/future.json", "{}")

    resp = query._get_data_range()
    body = json.loads(resp["body"])

    assert body["max_ts"] == "2026-04-01T12:00:00Z"


# ---- handler ----


def test_handler_mode_range_delegates(query):
    event = _admin_event(queryStringParameters={"mode": "range"})
    resp = query.handler(event, None)
    assert resp["statusCode"] == 200
    assert "min_ts" in json.loads(resp["body"])


def test_handler_requires_both_start_and_end(query):
    event = _admin_event(queryStringParameters={"start_time": "2026-03-01T00:00:00Z"})
    resp = query.handler(event, None)
    assert resp["statusCode"] == 400


def test_handler_rejects_non_iso_time(query):
    event = _admin_event(queryStringParameters={"start_time": "not-iso", "end_time": "2026-03-01T00:00:00Z"})
    resp = query.handler(event, None)
    assert resp["statusCode"] == 400


def test_handler_rejects_invalid_hours(query):
    event = _admin_event(queryStringParameters={"hours": "abc"})
    resp = query.handler(event, None)
    assert resp["statusCode"] == 400


def test_handler_rejects_invalid_type(query, monkeypatch):
    monkeypatch.setattr(query, "datetime", _FrozenDateTime)
    event = _admin_event(queryStringParameters={"type": "not-a-type"})
    resp = query.handler(event, None)
    assert resp["statusCode"] == 400


def test_handler_starts_query_execution(query, monkeypatch):
    monkeypatch.setattr(query, "datetime", _FrozenDateTime)
    monkeypatch.setattr(query.athena, "start_query_execution", lambda **kw: {"QueryExecutionId": "eid-123"})

    resp = query.handler(_admin_event(), None)

    assert resp["statusCode"] == 200
    assert json.loads(resp["body"]) == {"execution_id": "eid-123", "status": "RUNNING"}


def test_handler_reports_500_when_athena_start_fails(query, monkeypatch):
    monkeypatch.setattr(query, "datetime", _FrozenDateTime)

    def _boom(**kw):
        raise RuntimeError("boom")

    monkeypatch.setattr(query.athena, "start_query_execution", _boom)

    resp = query.handler(_admin_event(), None)

    assert resp["statusCode"] == 500


def test_handler_execution_id_running(query, monkeypatch):
    monkeypatch.setattr(
        query.athena,
        "get_query_execution",
        lambda **kw: {"QueryExecution": {"Status": {"State": "RUNNING"}}},
    )
    event = _admin_event(queryStringParameters={"execution_id": "eid-123"})

    resp = query.handler(event, None)

    assert json.loads(resp["body"]) == {"execution_id": "eid-123", "status": "RUNNING"}


def test_handler_execution_id_failed(query, monkeypatch):
    monkeypatch.setattr(
        query.athena,
        "get_query_execution",
        lambda **kw: {"QueryExecution": {"Status": {"State": "FAILED", "StateChangeReason": "syntax error"}}},
    )
    event = _admin_event(queryStringParameters={"execution_id": "eid-123"})

    resp = query.handler(event, None)

    body = json.loads(resp["body"])
    assert body["status"] == "FAILED"
    assert body["error"] == "syntax error"


def test_handler_execution_id_succeeded_includes_labels(query, monkeypatch):
    _put(query.S3_BUCKET, "labels/.json", json.dumps({"dev1": "リビング"}))
    monkeypatch.setattr(
        query.athena,
        "get_query_execution",
        lambda **kw: {"QueryExecution": {"Status": {"State": "SUCCEEDED"}}},
    )
    monkeypatch.setattr(query, "_parse_results", lambda eid: [{"ts": "1"}])

    event = _admin_event(queryStringParameters={"execution_id": "eid-123"})
    resp = query.handler(event, None)

    body = json.loads(resp["body"])
    assert body["status"] == "SUCCEEDED"
    assert body["data"] == [{"ts": "1"}]
    assert body["labels"] == {"dev1": "リビング"}
