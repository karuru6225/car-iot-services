import os

import boto3


def _get_watermark(device_id: str) -> dict:
    table = boto3.resource("dynamodb").Table(os.environ["WATERMARK_TABLE"])
    return table.get_item(Key={"device_id": device_id}).get("Item", {})


def _reading(ts: int, valid: bool = True) -> dict:
    return {"ts": ts, "rpm": 1000, "speedKmh": 0, "valid": valid}


# ---- _update_watermark ----


def test_first_batch_all_valid_opens_session(obd_ingest):
    obd_ingest._update_watermark("dev1", [_reading(100), _reading(102), _reading(104)])

    item = _get_watermark("dev1")
    assert item["last_ts"] == 104
    assert item["last_valid"] is True
    assert item["session_start"] == 100
    assert item["open_marker"] == "OPEN"
    assert "invalid_since" not in item


def test_continuing_valid_batch_keeps_session_start(obd_ingest):
    obd_ingest._update_watermark("dev1", [_reading(100)])
    obd_ingest._update_watermark("dev1", [_reading(102), _reading(104)])

    item = _get_watermark("dev1")
    assert item["last_ts"] == 104
    assert item["session_start"] == 100  # 変わらない
    assert "invalid_since" not in item


def test_transition_to_invalid_sets_invalid_since(obd_ingest):
    obd_ingest._update_watermark("dev1", [_reading(100), _reading(102)])
    # 102の次、104でvalid=falseに転じる（尾流しのイメージ）
    obd_ingest._update_watermark("dev1", [_reading(104, valid=False), _reading(106, valid=False)])

    item = _get_watermark("dev1")
    assert item["last_ts"] == 106
    assert item["last_valid"] is False
    assert item["invalid_since"] == 104  # 最初にfalseへ転じた時刻
    assert item["session_start"] == 100  # trip_sweepが処理するまで保持される
    assert item["open_marker"] == "OPEN"


def test_invalid_since_not_overwritten_while_continuing_invalid(obd_ingest):
    obd_ingest._update_watermark("dev1", [_reading(100, valid=False)])
    obd_ingest._update_watermark("dev1", [_reading(102, valid=False), _reading(104, valid=False)])

    item = _get_watermark("dev1")
    assert item["invalid_since"] == 100  # 最初のバッチの時刻のまま


def test_new_valid_reading_after_processed_session_starts_fresh(obd_ingest):
    # trip_sweepが処理済みにした後の状態を模擬（open_marker/session_start/invalid_since削除）
    table = boto3.resource("dynamodb").Table(os.environ["WATERMARK_TABLE"])
    table.update_item(
        Key={"device_id": "dev1"},
        UpdateExpression="SET last_ts = :t, last_valid = :v REMOVE open_marker, session_start, invalid_since",
        ExpressionAttributeValues={":t": 100, ":v": False},
    )

    obd_ingest._update_watermark("dev1", [_reading(500)])

    item = _get_watermark("dev1")
    assert item["session_start"] == 500  # 新しいセッションとして開始
    assert item["open_marker"] == "OPEN"
    assert "invalid_since" not in item


def test_first_ever_reading_for_device_defaults_last_valid_true(obd_ingest):
    # アイテムが存在しない状態でvalid=falseのreadingだけが来た場合、
    # 直前状態（last_valid）は不明なため楽観的にTrue扱いとし、
    # 「true→falseへ転じた」とみなしてinvalid_sinceをセットする
    # （session_startは一度もvalid=trueが無いためセットされない。
    # trip_sweep側はsession_start欠如を「分析対象なし」として扱う想定、PR2で実装）
    obd_ingest._update_watermark("dev-new", [_reading(100, valid=False)])

    item = _get_watermark("dev-new")
    assert item["last_valid"] is False
    assert item["invalid_since"] == 100
    assert "session_start" not in item


def test_out_of_order_readings_are_sorted_by_ts(obd_ingest):
    obd_ingest._update_watermark("dev1", [_reading(104), _reading(100), _reading(102)])

    item = _get_watermark("dev1")
    assert item["last_ts"] == 104  # 並び替え後の最後（最大ts）
    assert item["session_start"] == 100  # 並び替え後の最初（最小ts）


# ---- handler（S3書き込み成功後にウォッチ更新が呼ばれる統合テスト） ----


def test_handler_updates_watermark_after_s3_write(obd_ingest):
    event = {
        "requestContext": {"authorizer": {"jwt": {"claims": {"sub": "user-1"}}}},
        "body": '{"device_id": "car-iot-abc123", "readings": [{"ts": 100, "rpm": 900, "speedKmh": 0, "valid": true}]}',
    }
    resp = obd_ingest.handler(event, None)

    assert resp["statusCode"] == 200
    item = _get_watermark("car-iot-abc123")
    assert item["last_ts"] == 100
    assert item["session_start"] == 100
