import os

import boto3
import pytest


def _watermark_table():
    return boto3.resource("dynamodb").Table(os.environ["WATERMARK_TABLE"])


def _trip_summary_table():
    return boto3.resource("dynamodb").Table(os.environ["TRIP_SUMMARY_TABLE"])


def _put_watermark(device_id: str, **attrs):
    item = {"device_id": device_id, **attrs}
    _watermark_table().put_item(Item=item)


def _athena_col_row(labels):
    return {"Data": [{"VarCharValue": lbl} for lbl in labels]}


def _athena_data_row(values):
    return {"Data": [{"VarCharValue": None if v is None else str(v)} for v in values]}


def _fake_athena(monkeypatch, module, col_labels, data_rows, state="SUCCEEDED"):
    monkeypatch.setattr(
        module.athena, "start_query_execution", lambda **kw: {"QueryExecutionId": "eid-1"}
    )
    monkeypatch.setattr(
        module.athena,
        "get_query_execution",
        lambda **kw: {"QueryExecution": {"Status": {"State": state}}},
    )
    rows = [_athena_col_row(col_labels)] + [_athena_data_row(r) for r in data_rows]
    monkeypatch.setattr(
        module.athena,
        "get_query_results",
        lambda **kw: {
            "ResultSet": {
                "ResultSetMetadata": {"ColumnInfo": [{"Label": lbl} for lbl in col_labels]},
                "Rows": rows,
            }
        },
    )


# ---- _is_session_ended ----


def test_not_ended_when_recent_and_valid(trip_sweep):
    item = {"last_ts": 1000, "last_valid": True}
    assert trip_sweep._is_session_ended(item, now=1005) is False


def test_ended_by_gap_timeout(trip_sweep):
    item = {"last_ts": 1000, "last_valid": True}
    assert trip_sweep._is_session_ended(item, now=1000 + 600) is True
    assert trip_sweep._is_session_ended(item, now=1000 + 599) is False


def test_ended_by_invalid_tail(trip_sweep):
    item = {"last_ts": 1000, "last_valid": False, "invalid_since": 1000}
    assert trip_sweep._is_session_ended(item, now=1005) is True
    assert trip_sweep._is_session_ended(item, now=1004) is False


def test_not_ended_when_invalid_but_no_invalid_since(trip_sweep):
    # 理論上は起きない想定だが、防御的に：invalid_sinceが無ければgapタイムアウトのみで判定
    item = {"last_ts": 1000, "last_valid": False}
    assert trip_sweep._is_session_ended(item, now=1005) is False


# ---- _open_sessions（GSI Query） ----


def test_open_sessions_returns_only_open_marker_items(trip_sweep):
    _put_watermark("dev1", last_ts=100, last_valid=True, open_marker="OPEN")
    _put_watermark("dev2", last_ts=200, last_valid=True)  # open_marker無し（処理済み）

    items = trip_sweep._open_sessions()

    assert [i["device_id"] for i in items] == ["dev1"]


# ---- _compute_summary ----


def test_compute_summary_calculates_distance_fuel_and_stats(trip_sweep):
    rows = [
        {"obd_ts": 100, "lat": 35.0, "lon": 139.0, "fuel_rate_lph": 2.0,
         "ltft_pct": -5.0, "stft_pct": -2.0, "catalyst_temp_c": 400.0, "boost_kpa": 10.0, "coolant_c": 60.0},
        {"obd_ts": 102, "lat": 35.001, "lon": 139.0, "fuel_rate_lph": 4.0,
         "ltft_pct": -7.0, "stft_pct": 2.0, "catalyst_temp_c": 420.0, "boost_kpa": 30.0, "coolant_c": 65.0},
    ]
    summary = trip_sweep._compute_summary(rows)

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


def test_compute_summary_handles_missing_gps_and_zero_fuel(trip_sweep):
    rows = [
        {"obd_ts": 100, "lat": None, "lon": None, "fuel_rate_lph": 0.0,
         "ltft_pct": None, "stft_pct": None, "catalyst_temp_c": None, "boost_kpa": None, "coolant_c": None},
        {"obd_ts": 110, "lat": None, "lon": None, "fuel_rate_lph": 0.0,
         "ltft_pct": None, "stft_pct": None, "catalyst_temp_c": None, "boost_kpa": None, "coolant_c": None},
    ]
    summary = trip_sweep._compute_summary(rows)

    assert summary["distance_km"] == 0.0
    assert summary["fuel_l"] == 0.0
    assert summary["fuel_economy_km_l"] is None  # fuel_l=0は0除算を避けてNone
    assert summary["ltft_avg"] is None
    assert summary["catalyst_temp_max"] is None


# ---- _save_trip_summary / _close_watermark ----


def test_save_trip_summary_writes_item(trip_sweep):
    trip_sweep._save_trip_summary("dev1", 100, 200, {"distance_km": 1.5, "fuel_economy_km_l": None})

    item = _trip_summary_table().get_item(Key={"device_id": "dev1", "session_start": 100}).get("Item")
    assert item["session_end"] == 200
    assert item["distance_km"] == pytest.approx(1.5)
    assert "fuel_economy_km_l" not in item  # Noneのキーは書き込まない
    assert item["narrative"] == ""


def test_close_watermark_removes_open_attributes(trip_sweep):
    _put_watermark("dev1", last_ts=100, last_valid=True, open_marker="OPEN", session_start=50, invalid_since=90)

    trip_sweep._close_watermark("dev1")

    item = _watermark_table().get_item(Key={"device_id": "dev1"}).get("Item")
    assert "open_marker" not in item
    assert "session_start" not in item
    assert "invalid_since" not in item
    assert item["last_ts"] == 100  # last_ts/last_validは保持される


# ---- handler（統合） ----


def test_handler_summarizes_ended_session_and_closes_watermark(trip_sweep, monkeypatch):
    _put_watermark("dev1", last_ts=1000, last_valid=True, open_marker="OPEN", session_start=900)
    monkeypatch.setattr(trip_sweep.time, "time", lambda: 1000 + trip_sweep.GAP_TIMEOUT_SEC)

    _fake_athena(
        monkeypatch, trip_sweep,
        col_labels=["obd_ts", "lat", "lon", "maf_gs", "fuel_rate_lph", "ltft_pct", "stft_pct",
                    "catalyst_temp_c", "boost_kpa", "coolant_c"],
        data_rows=[[900, 35.0, 139.0, 2.0, 1.0, -5.0, 0.0, 400.0, 10.0, 60.0],
                   [1000, 35.001, 139.0, 2.5, 1.2, -5.0, 0.0, 410.0, 12.0, 62.0]],
    )

    result = trip_sweep.handler({}, None)

    assert result == {"processed": 1, "skipped": 0}
    saved = _trip_summary_table().get_item(Key={"device_id": "dev1", "session_start": 900}).get("Item")
    assert saved is not None
    watermark = _watermark_table().get_item(Key={"device_id": "dev1"}).get("Item")
    assert "open_marker" not in watermark


def test_handler_skips_not_yet_ended_session(trip_sweep, monkeypatch):
    _put_watermark("dev1", last_ts=1000, last_valid=True, open_marker="OPEN", session_start=900)
    monkeypatch.setattr(trip_sweep.time, "time", lambda: 1005)  # gapタイムアウトに満たない

    result = trip_sweep.handler({}, None)

    assert result == {"processed": 0, "skipped": 0}
    watermark = _watermark_table().get_item(Key={"device_id": "dev1"}).get("Item")
    assert watermark["open_marker"] == "OPEN"  # 未処理のまま


def test_handler_closes_watermark_without_summary_when_session_start_missing(trip_sweep, monkeypatch):
    # 一度もvalid=trueが無いまま終了したケース（PR1のobd_ingestテスト参照）
    _put_watermark("dev1", last_ts=1000, last_valid=False, invalid_since=1000, open_marker="OPEN")
    monkeypatch.setattr(trip_sweep.time, "time", lambda: 1000 + trip_sweep.INVALID_TAIL_SEC)

    result = trip_sweep.handler({}, None)

    assert result == {"processed": 0, "skipped": 1}
    watermark = _watermark_table().get_item(Key={"device_id": "dev1"}).get("Item")
    assert "open_marker" not in watermark
    assert _trip_summary_table().get_item(Key={"device_id": "dev1", "session_start": 0}).get("Item") is None


def test_handler_leaves_watermark_open_when_athena_fails(trip_sweep, monkeypatch):
    _put_watermark("dev1", last_ts=1000, last_valid=True, open_marker="OPEN", session_start=900)
    monkeypatch.setattr(trip_sweep.time, "time", lambda: 1000 + trip_sweep.GAP_TIMEOUT_SEC)

    def _boom(**kw):
        raise RuntimeError("athena unavailable")

    monkeypatch.setattr(trip_sweep.athena, "start_query_execution", _boom)

    result = trip_sweep.handler({}, None)

    assert result == {"processed": 0, "skipped": 0}
    watermark = _watermark_table().get_item(Key={"device_id": "dev1"}).get("Item")
    assert watermark["open_marker"] == "OPEN"  # 次回スイープでリトライさせるため閉じない
