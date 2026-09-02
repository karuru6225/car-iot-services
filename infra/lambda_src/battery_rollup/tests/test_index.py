import json
import os
from datetime import datetime, timedelta, timezone

import boto3


def _s3():
    return boto3.client("s3")


def _put_raw_object(dt: datetime, device_id="dev1"):
    """raw/のhourパーティションにダミーオブジェクトを1つ置く（_earliest_battery_date用）。"""
    key = f"raw/year={dt.year:04d}/month={dt.month:02d}/day={dt.day:02d}/hour={dt.hour:02d}/{device_id}-x.json"
    _s3().put_object(Bucket=os.environ["S3_BUCKET"], Key=key, Body=b"{}")


def _read_rollup_file(date_str: str, battery_rollup):
    """NDJSON（改行区切り、1行1レコード）として読み、dictのリストで返す。"""
    obj = _s3().get_object(Bucket=os.environ["S3_BUCKET"], Key=battery_rollup._rollup_key(date_str))
    body = obj["Body"].read().decode("utf-8")
    return [json.loads(line) for line in body.splitlines() if line.strip()]


# ---- 0. rollup/のS3キーは year=/month= でパーティション分割される ----


def test_rollup_key_partitions_by_year_and_month(battery_rollup):
    assert battery_rollup._rollup_key("2026-05-15") == "rollup/year=2026/month=05/2026-05-15.json"


# ---- 1. JST変換の境界値 ----


def test_jst_date_str_just_before_jst_midnight(battery_rollup):
    dt = datetime(2026, 8, 30, 14, 59, 0, tzinfo=timezone.utc)  # JST 2026-08-30 23:59
    assert battery_rollup._jst_date_str(dt) == "2026-08-30"


def test_jst_date_str_just_after_jst_midnight(battery_rollup):
    dt = datetime(2026, 8, 30, 15, 0, 0, tzinfo=timezone.utc)  # JST 2026-08-31 00:00
    assert battery_rollup._jst_date_str(dt) == "2026-08-31"


def test_jst_day_start_utc_matches_jst_date_str(battery_rollup):
    start = battery_rollup._jst_day_start_utc("2026-08-31")
    assert start == datetime(2026, 8, 30, 15, 0, 0, tzinfo=timezone.utc)
    assert battery_rollup._jst_date_str(start) == "2026-08-31"


# ---- 2. charge/discharge の符号分離 ----


def test_build_query_separates_charge_and_discharge_by_sign(battery_rollup):
    query = battery_rollup._build_query(["2026-08-29"])
    assert "WHEN delta_ah > 0 THEN delta_ah" in query
    assert "WHEN delta_ah < 0 THEN -delta_ah" in query


def test_group_by_date_passes_through_presigned_values(battery_rollup):
    # SQL側で既に符号分離済みの値（Athena結果を模したdict）がそのまま日付ごとに渡ること
    rows = [{"jst_date": "2026-08-29", "device_id": "dev1", "charge_ah": 5.0, "discharge_ah": 3.0, "row_count": 1440.0}]
    grouped = battery_rollup._group_by_date(rows, ["2026-08-29"])
    assert grouped["2026-08-29"] == [
        {"date": "2026-08-29", "device_id": "dev1", "charge_ah": 5.0, "discharge_ah": 3.0, "row_count": 1440},
    ]


# ---- 3. 複数device_id混在 ----


def test_group_by_date_keeps_multiple_devices_separate(battery_rollup):
    rows = [
        {"jst_date": "2026-08-29", "device_id": "dev1", "charge_ah": 5.0, "discharge_ah": 3.0, "row_count": 100.0},
        {"jst_date": "2026-08-29", "device_id": "dev2", "charge_ah": 1.0, "discharge_ah": 0.5, "row_count": 90.0},
    ]
    grouped = battery_rollup._group_by_date(rows, ["2026-08-29"])
    device_ids = {r["device_id"] for r in grouped["2026-08-29"]}
    assert device_ids == {"dev1", "dev2"}
    assert len(grouped["2026-08-29"]) == 2


# ---- 4. シード日が出力から除外される ----


def test_group_by_date_drops_rows_outside_target_dates(battery_rollup):
    rows = [
        {"jst_date": "2026-08-28", "device_id": "dev1", "charge_ah": 1.0, "discharge_ah": 0.0, "row_count": 1.0},  # シード日
        {"jst_date": "2026-08-29", "device_id": "dev1", "charge_ah": 2.0, "discharge_ah": 0.0, "row_count": 1.0},
    ]
    grouped = battery_rollup._group_by_date(rows, ["2026-08-29"])
    assert "2026-08-28" not in grouped
    assert len(grouped["2026-08-29"]) == 1


# ---- 5. 日境界をまたぐdeltaを欠落させないための、シード日を含んだクエリ範囲 ----


def test_build_query_range_includes_seed_day_before_first_target(battery_rollup):
    query = battery_rollup._build_query(["2026-08-29", "2026-08-30"])
    # シード日(08-28)の JST 00:00 = UTC 08-27 15:00 から開始する
    assert "ts >= '2026-08-27T15:00:00Z'" in query
    # 対象最終日(08-30)の翌日(08-31) JST 00:00 = UTC 08-30 15:00 で排他的に終わる
    assert "ts < '2026-08-30T15:00:00Z'" in query


# ---- 6. watermark逆算・初回バックフィル ----


def test_target_dates_first_backfill_starts_from_earliest_battery_date(battery_rollup):
    _put_raw_object(datetime(2026, 6, 1, 3, tzinfo=timezone.utc))
    now_utc = datetime(2026, 6, 5, 3, tzinfo=timezone.utc)  # JST 2026-06-05 12:00 -> 前日は06-04

    dates = battery_rollup._target_dates(now_utc, {})

    assert dates[0] == "2026-06-01"
    assert dates[-1] == "2026-06-04"


def test_target_dates_returns_empty_when_no_raw_data(battery_rollup):
    now_utc = datetime(2026, 6, 5, 3, tzinfo=timezone.utc)
    assert battery_rollup._target_dates(now_utc, {}) == []


# ---- 7. watermark逆算・継続実行 ----


def test_target_dates_continuation_rewinds_by_reprocess_lookback_days(battery_rollup):
    battery_rollup._write_rollup_file("2026-06-10", [])
    now_utc = datetime(2026, 6, 15, 3, tzinfo=timezone.utc)  # 前日は06-14

    dates = battery_rollup._target_dates(now_utc, {})

    # REPROCESS_LOOKBACK_DAYS=3（conftest.py）なので 06-10 - 3日 = 06-07 から始まる
    assert dates[0] == "2026-06-07"
    assert dates[-1] == "2026-06-14"


# ---- 8. MAX_DAYS_PER_RUN による打ち切り ----


def test_target_dates_truncates_to_max_days_per_run_oldest_first(battery_rollup):
    _put_raw_object(datetime(2026, 1, 1, 0, tzinfo=timezone.utc))
    now_utc = datetime(2026, 6, 5, 3, tzinfo=timezone.utc)  # 履歴がMAX_DAYS_PER_RUN(30)を大きく超える

    dates = battery_rollup._target_dates(now_utc, {})

    assert len(dates) == 30
    assert dates[0] == "2026-01-01"  # 古い方から優先


# ---- 9. re-run時に上書き（追記ではない） ----


def test_write_rollup_file_overwrites_not_appends(battery_rollup):
    battery_rollup._write_rollup_file("2026-08-29", [{"date": "2026-08-29", "device_id": "dev1", "charge_ah": 1.0, "discharge_ah": 0.0, "row_count": 1}])
    battery_rollup._write_rollup_file("2026-08-29", [{"date": "2026-08-29", "device_id": "dev1", "charge_ah": 9.0, "discharge_ah": 0.0, "row_count": 2}])

    content = _read_rollup_file("2026-08-29", battery_rollup)
    assert len(content) == 1
    assert content[0]["charge_ah"] == 9.0


# ---- 10. 対象日に結果0件でも空配列で書かれ、watermarkが前進する ----


def test_write_rollup_file_writes_empty_array_and_advances_watermark(battery_rollup):
    assert battery_rollup._latest_rollup_date() is None

    battery_rollup._write_rollup_file("2026-08-29", [])

    assert _read_rollup_file("2026-08-29", battery_rollup) == []
    assert battery_rollup._latest_rollup_date() == "2026-08-29"


def test_handler_writes_empty_file_when_athena_returns_no_rows(battery_rollup, monkeypatch):
    monkeypatch.setattr(battery_rollup, "_run_athena_query", lambda query: [])

    result = battery_rollup.handler({"since": "2026-08-29", "until": "2026-08-29"}, None)

    assert result == {"target_dates": ["2026-08-29"], "written": 1}
    assert _read_rollup_file("2026-08-29", battery_rollup) == []


# ---- 11. ah が null の行が混在してもクラッシュしない（SQL側のWHERE句で防ぐ） ----


def test_build_query_excludes_null_ah_rows(battery_rollup):
    query = battery_rollup._build_query(["2026-08-29"])
    assert "ah IS NOT NULL" in query


# ---- 12. handler の event 分岐 ----


def test_target_dates_uses_explicit_since_until_when_given(battery_rollup):
    # rollup/やraw/の状態に関わらず、event.since/untilが最優先される
    dates = battery_rollup._target_dates(
        datetime(2026, 6, 5, 3, tzinfo=timezone.utc),
        {"since": "2026-01-10", "until": "2026-01-12"},
    )
    assert dates == ["2026-01-10", "2026-01-11", "2026-01-12"]


def test_target_dates_falls_back_to_watermark_when_event_empty(battery_rollup):
    _put_raw_object(datetime(2026, 6, 1, 0, tzinfo=timezone.utc))
    dates = battery_rollup._target_dates(datetime(2026, 6, 5, 3, tzinfo=timezone.utc), {})
    assert dates[0] == "2026-06-01"


# ---- 13. 複数日にまたがる継続実行（2日間シナリオ） ----


def test_target_dates_two_day_run_covers_old_gap_and_new_day_without_overlap_loss(battery_rollup):
    """1日目: rollup/が空の状態から実行し、MAX_DAYS_PER_RUN(30)により打ち切られて
    2026-06-01〜2026-06-30だけが書き込まれる（履歴は2026-06-01〜2026-07-10の40日分）。
    2日目: 日付が1日進んだ状態で実行すると、1日目の続き（まだrollupされていない
    07-01〜07-10）と、2日目時点で新たに対象になった直近の日（07-11）の両方が、
    1回の_target_dates呼び出しでひと続きの範囲として欠落なく対象になること。"""
    _put_raw_object(datetime(2026, 6, 1, 0, tzinfo=timezone.utc))

    # 1日目: JST 2026-07-11 → 前日は2026-07-10。履歴40日分がMAX_DAYS_PER_RUN(30)で打ち切られる
    now_utc_day1 = datetime(2026, 7, 11, 3, tzinfo=timezone.utc)
    day1_dates = battery_rollup._target_dates(now_utc_day1, {})
    assert day1_dates[0] == "2026-06-01"
    assert day1_dates[-1] == "2026-06-30"
    assert len(day1_dates) == 30

    for d in day1_dates:
        battery_rollup._write_rollup_file(d, [])

    # 2日目: 日付が1日進む → JST 2026-07-12 → 前日は2026-07-11（新たに対象になる日）
    now_utc_day2 = now_utc_day1 + timedelta(days=1)
    day2_dates = battery_rollup._target_dates(now_utc_day2, {})

    # REPROCESS_LOOKBACK_DAYS=3分遡った06-27から、新しい前日07-11まで、隙間なく連続
    assert day2_dates[0] == "2026-06-27"
    assert day2_dates[-1] == "2026-07-11"
    assert day2_dates == battery_rollup._date_range("2026-06-27", "2026-07-11")
    # 1日目の続き（打ち切られて未処理のまま残った07-01〜07-10）が含まれること
    assert "2026-07-05" in day2_dates
    # 2日目時点で新たに対象になった直近の日が含まれること
    assert "2026-07-11" in day2_dates
