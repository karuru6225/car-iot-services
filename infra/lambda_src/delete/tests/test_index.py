import json
from datetime import datetime, timezone

import boto3
import pytest


def _admin_event(**overrides):
    event = {
        "requestContext": {"authorizer": {"jwt": {"claims": {"cognito:groups": "[admin]"}}}},
        "queryStringParameters": {},
    }
    event.update(overrides)
    return event


def _put(bucket, key, body):
    boto3.client("s3").put_object(Bucket=bucket, Key=key, Body=body.encode("utf-8"))


def _keys(bucket, prefix=""):
    resp = boto3.client("s3").list_objects_v2(Bucket=bucket, Prefix=prefix)
    return sorted(o["Key"] for o in resp.get("Contents", []))


class _FrozenDateTime(datetime):
    @classmethod
    def now(cls, tz=None):
        return datetime(2026, 3, 1, 12, 0, tzinfo=tz)


# ---- _ts_ago ----


def test_ts_ago_formats_iso(delete, monkeypatch):
    monkeypatch.setattr(delete, "datetime", _FrozenDateTime)
    assert delete._ts_ago(2) == "2026-03-01T10:00:00Z"


# ---- _partition_filters ----


def test_partition_filters_includes_hour_within_72h(delete, monkeypatch):
    monkeypatch.setattr(delete, "datetime", _FrozenDateTime)
    filters = delete._partition_filters(2)
    assert any(f.startswith("hour") for f in filters)


def test_partition_filters_omits_hour_beyond_72h(delete, monkeypatch):
    monkeypatch.setattr(delete, "datetime", _FrozenDateTime)
    filters = delete._partition_filters(200)
    assert not any(f.startswith("hour") for f in filters)


def test_partition_filters_includes_hour_at_exactly_72h(delete, monkeypatch):
    monkeypatch.setattr(delete, "datetime", _FrozenDateTime)
    filters = delete._partition_filters(72)
    assert any(f.startswith("hour") for f in filters)


def test_partition_filters_omits_hour_at_73h(delete, monkeypatch):
    monkeypatch.setattr(delete, "datetime", _FrozenDateTime)
    filters = delete._partition_filters(73)
    assert not any(f.startswith("hour") for f in filters)


def test_partition_filters_single_value_uses_equals(delete, monkeypatch):
    monkeypatch.setattr(delete, "datetime", _FrozenDateTime)
    filters = delete._partition_filters(1)
    assert "year = '2026'" in filters


# ---- _parse_records ----


def test_parse_records_single_json(delete):
    body = json.dumps({"ts": "1", "addr": "AA:BB"}).encode("utf-8")
    assert delete._parse_records(body) == [{"ts": "1", "addr": "AA:BB"}]


def test_parse_records_ndjson(delete):
    body = (json.dumps({"ts": "1"}) + "\n" + json.dumps({"ts": "2"}) + "\n").encode("utf-8")
    assert delete._parse_records(body) == [{"ts": "1"}, {"ts": "2"}]


def test_parse_records_skips_invalid_lines(delete):
    body = (json.dumps({"ts": "1"}) + "\nnot json\n" + json.dumps({"ts": "2"}) + "\n").encode("utf-8")
    assert delete._parse_records(body) == [{"ts": "1"}, {"ts": "2"}]


def test_parse_records_all_invalid_returns_empty(delete):
    assert delete._parse_records(b"not json at all") == []


# ---- _is_admin ----


def test_is_admin_true_when_group_present(delete):
    assert delete._is_admin(_admin_event()) is True


def test_is_admin_false_when_group_absent(delete):
    event = {"requestContext": {"authorizer": {"jwt": {"claims": {"cognito:groups": "[user]"}}}}}
    assert delete._is_admin(event) is False


def test_is_admin_false_when_claims_missing(delete):
    assert delete._is_admin({}) is False


# ---- _delete_from_partition ----


def test_delete_from_partition_matches_addr_single_json(delete):
    prefix = "raw/year=2026/month=03/day=01/hour=10/"
    _put(delete.S3_BUCKET, prefix + "dev-a.json", json.dumps({"addr": "AA:BB"}))
    _put(delete.S3_BUCKET, prefix + "dev-b.json", json.dumps({"addr": "CC:DD"}))

    deleted = delete._delete_from_partition("AA:BB", None, "2026", "03", "01", "10")

    assert deleted == 1
    assert _keys(delete.S3_BUCKET, prefix) == [prefix + "dev-b.json"]


def test_delete_from_partition_ndjson_file_removed_wholesale(delete):
    prefix = "raw/year=2026/month=03/day=01/hour=10/"
    body = json.dumps({"addr": "AA:BB"}) + "\n" + json.dumps({"addr": "CC:DD"}) + "\n"
    _put(delete.S3_BUCKET, prefix + "merged.json", body)

    deleted = delete._delete_from_partition("AA:BB", None, "2026", "03", "01", "10")

    # 1行だけの一致でもファイル全体が削除される（他の非一致行も道連れ）
    assert deleted == 1
    assert _keys(delete.S3_BUCKET, prefix) == []


def test_delete_from_partition_matches_by_id(delete):
    prefix = "raw/year=2026/month=03/day=01/hour=10/"
    _put(delete.S3_BUCKET, prefix + "dev-a.json", json.dumps({"id": "voltage_1"}))

    deleted = delete._delete_from_partition(None, "voltage_1", "2026", "03", "01", "10")

    assert deleted == 1
    assert _keys(delete.S3_BUCKET, prefix) == []


def test_delete_from_partition_no_match_leaves_files(delete):
    prefix = "raw/year=2026/month=03/day=01/hour=10/"
    _put(delete.S3_BUCKET, prefix + "dev-a.json", json.dumps({"addr": "CC:DD"}))

    deleted = delete._delete_from_partition("AA:BB", None, "2026", "03", "01", "10")

    assert deleted == 0
    assert _keys(delete.S3_BUCKET, prefix) == [prefix + "dev-a.json"]


# ---- _delete_by_keys ----


def test_delete_by_keys_accepts_plain_and_s3_uri_keys(delete):
    key = "raw/year=2026/month=03/day=01/hour=10/dev-a.json"
    _put(delete.S3_BUCKET, key, json.dumps({"addr": "AA:BB"}))

    resp = delete._delete_by_keys([f"s3://{delete.S3_BUCKET}/{key}"])

    assert json.loads(resp["body"]) == {"deleted": 1}
    assert _keys(delete.S3_BUCKET) == []


def test_delete_by_keys_rejects_invalid_keys(delete):
    ok_key = "raw/year=2026/month=03/day=01/hour=10/ok.json"
    _put(delete.S3_BUCKET, ok_key, json.dumps({"addr": "AA:BB"}))

    resp = delete._delete_by_keys(["../../etc/passwd", ok_key])

    # パストラバーサルのような不正キーは無視され、有効なキーだけ削除される
    assert json.loads(resp["body"]) == {"deleted": 1}


# ---- handler ----


def test_handler_rejects_non_admin(delete):
    event = {"requestContext": {"authorizer": {"jwt": {"claims": {"cognito:groups": "[user]"}}}}}
    resp = delete.handler(event, None)
    assert resp["statusCode"] == 403


def test_handler_requires_addr_or_id(delete):
    resp = delete.handler(_admin_event(), None)
    assert resp["statusCode"] == 400


def test_handler_rejects_invalid_addr(delete):
    event = _admin_event(queryStringParameters={"addr": "not-a-mac"})
    resp = delete.handler(event, None)
    assert resp["statusCode"] == 400


def test_handler_rejects_invalid_id(delete):
    event = _admin_event(queryStringParameters={"id": "bad id!"})
    resp = delete.handler(event, None)
    assert resp["statusCode"] == 400


def test_handler_rejects_invalid_hours(delete):
    event = _admin_event(queryStringParameters={"addr": "AA:BB:CC:DD:EE:FF", "hours": "abc"})
    resp = delete.handler(event, None)
    assert resp["statusCode"] == 400


def test_handler_s3_keys_path_bypasses_athena(delete):
    key = "raw/year=2026/month=03/day=01/hour=10/dev-a.json"
    _put(delete.S3_BUCKET, key, json.dumps({"addr": "AA:BB"}))
    event = _admin_event(body=json.dumps({"s3_keys": [key]}))

    resp = delete.handler(event, None)

    assert resp["statusCode"] == 200
    assert json.loads(resp["body"]) == {"deleted": 1}


def test_handler_addr_path_delegates_to_partitions(delete, monkeypatch):
    prefix = "raw/year=2026/month=03/day=01/hour=10/"
    _put(delete.S3_BUCKET, prefix + "dev-a.json", json.dumps({"addr": "AA:BB:CC:DD:EE:FF"}))
    monkeypatch.setattr(delete, "_get_partitions", lambda addr, sid, hours: [("2026", "03", "01", "10")])

    event = _admin_event(queryStringParameters={"addr": "AA:BB:CC:DD:EE:FF", "hours": "72"})
    resp = delete.handler(event, None)

    assert resp["statusCode"] == 200
    assert json.loads(resp["body"]) == {"deleted": 1}


def test_handler_reports_500_on_athena_error(delete, monkeypatch):
    def _boom(addr, sid, hours):
        raise RuntimeError("Athena FAILED: boom")

    monkeypatch.setattr(delete, "_get_partitions", _boom)
    event = _admin_event(queryStringParameters={"addr": "AA:BB:CC:DD:EE:FF"})

    resp = delete.handler(event, None)

    assert resp["statusCode"] == 500
