import json
from datetime import datetime, timezone

import boto3


def _put(bucket, key, body):
    boto3.client("s3").put_object(Bucket=bucket, Key=key, Body=body.encode("utf-8"))


def _get(bucket, key):
    return boto3.client("s3").get_object(Bucket=bucket, Key=key)["Body"].read().decode("utf-8")


def _keys(bucket, prefix=""):
    resp = boto3.client("s3").list_objects_v2(Bucket=bucket, Prefix=prefix)
    return sorted(o["Key"] for o in resp.get("Contents", []))


DT = datetime(2026, 3, 1, 10, tzinfo=timezone.utc)


def test_prefix_for(compact):
    assert compact._prefix_for(DT) == "raw/year=2026/month=03/day=01/hour=10/"


def test_target_hour_partitions_orders_newest_first(compact):
    since = datetime(2026, 3, 1, 0, tzinfo=timezone.utc)
    until = datetime(2026, 3, 1, 3, tzinfo=timezone.utc)
    partitions = compact._target_hour_partitions(since, until, max_partitions=200)
    assert partitions == [
        datetime(2026, 3, 1, 2, tzinfo=timezone.utc),
        datetime(2026, 3, 1, 1, tzinfo=timezone.utc),
        datetime(2026, 3, 1, 0, tzinfo=timezone.utc),
    ]


def test_target_hour_partitions_respects_max_partitions(compact):
    since = datetime(2026, 3, 1, 0, tzinfo=timezone.utc)
    until = datetime(2026, 3, 1, 10, tzinfo=timezone.utc)
    partitions = compact._target_hour_partitions(since, until, max_partitions=3)
    assert len(partitions) == 3


def test_compact_partition_no_files_is_skipped(compact):
    result = compact._compact_partition(DT)
    assert result["action"] == "skip"
    assert result["archived"] == 0


def test_compact_partition_single_straggler_is_left_alone(compact):
    prefix = compact._prefix_for(DT)
    _put(compact.S3_BUCKET, prefix + "device-abc.json", json.dumps({"ts": "x"}))

    result = compact._compact_partition(DT)

    assert result["action"] == "skip"
    assert result["archived"] == 0
    # マージする意味がないので元ファイルはそのまま残る
    assert _keys(compact.S3_BUCKET, prefix) == [prefix + "device-abc.json"]


def test_compact_partition_merges_multiple_stragglers(compact):
    prefix = compact._prefix_for(DT)
    _put(compact.S3_BUCKET, prefix + "device-a.json", json.dumps({"ts": "1"}))
    _put(compact.S3_BUCKET, prefix + "device-b.json", json.dumps({"ts": "2"}))

    result = compact._compact_partition(DT)

    assert result == {"partition": prefix, "action": "merged", "archived": 2}
    # 元ファイルはraw/から消え、merged.jsonだけが残る
    assert _keys(compact.S3_BUCKET, prefix) == [prefix + "merged.json"]
    merged_lines = [json.loads(line) for line in _get(compact.S3_BUCKET, prefix + "merged.json").splitlines()]
    assert {"ts": "1"} in merged_lines
    assert {"ts": "2"} in merged_lines
    # archiveバケットには元ファイルがそのまま1:1でコピーされている
    assert _keys(compact.ARCHIVE_BUCKET, prefix) == [prefix + "device-a.json", prefix + "device-b.json"]


def test_compact_partition_absorbs_into_existing_merged(compact):
    prefix = compact._prefix_for(DT)
    _put(compact.S3_BUCKET, prefix + "merged.json", json.dumps({"ts": "existing"}) + "\n")
    _put(compact.S3_BUCKET, prefix + "device-new.json", json.dumps({"ts": "new"}))

    result = compact._compact_partition(DT)

    assert result == {"partition": prefix, "action": "merged", "archived": 1}
    merged_lines = [json.loads(line) for line in _get(compact.S3_BUCKET, prefix + "merged.json").splitlines()]
    assert {"ts": "existing"} in merged_lines
    assert {"ts": "new"} in merged_lines
    assert _keys(compact.S3_BUCKET, prefix) == [prefix + "merged.json"]


def test_compact_partition_archives_unmergeable_invalid_json(compact):
    prefix = compact._prefix_for(DT)
    _put(compact.S3_BUCKET, prefix + "device-a.json", "not valid json")
    _put(compact.S3_BUCKET, prefix + "device-b.json", "also not valid")

    result = compact._compact_partition(DT)

    # 有効な行は0件なのでmerged.jsonは作られないが、raw/に残す理由もないのでarchiveはされる
    assert result == {"partition": prefix, "action": "skip", "archived": 2}
    assert _keys(compact.S3_BUCKET, prefix) == []
    assert _keys(compact.ARCHIVE_BUCKET, prefix) == [prefix + "device-a.json", prefix + "device-b.json"]


def test_compact_partition_is_idempotent(compact):
    prefix = compact._prefix_for(DT)
    _put(compact.S3_BUCKET, prefix + "device-a.json", json.dumps({"ts": "1"}))
    _put(compact.S3_BUCKET, prefix + "device-b.json", json.dumps({"ts": "2"}))

    first = compact._compact_partition(DT)
    second = compact._compact_partition(DT)

    assert first["action"] == "merged"
    assert second == {"partition": prefix, "action": "skip", "archived": 0}
    # 2回目で内容が壊れたり重複したりしていない
    merged_lines = _get(compact.S3_BUCKET, prefix + "merged.json").splitlines()
    assert len(merged_lines) == 2


def test_handler_uses_explicit_event_range(compact):
    prefix = compact._prefix_for(DT)
    _put(compact.S3_BUCKET, prefix + "device-a.json", json.dumps({"ts": "1"}))
    _put(compact.S3_BUCKET, prefix + "device-b.json", json.dumps({"ts": "2"}))

    event = {
        "since": "2026-03-01T09:00:00Z",
        "until": "2026-03-01T11:00:00Z",
        "max_partitions": 10,
    }
    result = compact.handler(event, None)

    assert result == {"partitions_scanned": 2, "partitions_merged": 1, "files_archived": 2}
