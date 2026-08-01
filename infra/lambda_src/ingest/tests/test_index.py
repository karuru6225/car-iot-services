import base64
import json
import struct
import zlib
from datetime import datetime, timezone

import boto3
import pytest


def _keys(bucket, prefix=""):
    resp = boto3.client("s3").list_objects_v2(Bucket=bucket, Prefix=prefix)
    return sorted(o["Key"] for o in resp.get("Contents", []))


def _body(bucket, key):
    return boto3.client("s3").get_object(Bucket=bucket, Key=key)["Body"].read().decode("utf-8")


class _FrozenDateTime(datetime):
    @classmethod
    def now(cls, tz=None):
        return datetime(2026, 3, 1, 10, 0, tzinfo=tz)


# ---- _expand_keys ----


def test_expand_keys_maps_short_names(ingest):
    assert ingest._expand_keys({"t": "battery", "m": 12.3, "unknown": "x"}) == {
        "type": "battery",
        "main": 12.3,
        "unknown": "x",
    }


# ---- _msgpack_unpack ----


def mp_fixstr(s: str) -> bytes:
    b = s.encode("utf-8")
    return bytes([0xA0 | len(b)]) + b


def mp_fixmap(pairs: list) -> bytes:
    out = bytes([0x80 | len(pairs)])
    for k, v in pairs:
        out += k + v
    return out


def mp_fixarray(items: list) -> bytes:
    out = bytes([0x90 | len(items)])
    for item in items:
        out += item
    return out


@pytest.mark.parametrize(
    "raw,expected",
    [
        (bytes([5]), 5),  # positive fixint
        (bytes([0xFF]), -1),  # negative fixint
        (bytes([0xE0]), -32),  # negative fixint (下限)
        (mp_fixstr("hi"), "hi"),
        (bytes([0xC0]), None),
        (bytes([0xC2]), False),
        (bytes([0xC3]), True),
        (bytes([0xCA]) + struct.pack(">f", 1.5), pytest.approx(1.5)),
        (bytes([0xCB]) + struct.pack(">d", 1.25), 1.25),
        (bytes([0xCC]) + struct.pack("B", 200), 200),
        (bytes([0xCD]) + struct.pack(">H", 40000), 40000),
        (bytes([0xCE]) + struct.pack(">I", 3_000_000_000), 3_000_000_000),
        (bytes([0xCF]) + struct.pack(">Q", 10_000_000_000), 10_000_000_000),
        (bytes([0xD0]) + struct.pack("b", -100), -100),
        (bytes([0xD1]) + struct.pack(">h", -20000), -20000),
        (bytes([0xD2]) + struct.pack(">i", -70000), -70000),
        (bytes([0xD9, 2]) + b"hi", "hi"),  # str8
    ],
)
def test_msgpack_unpack_scalars(ingest, raw, expected):
    assert ingest._msgpack_unpack(raw) == expected


def test_msgpack_unpack_fixmap(ingest):
    raw = mp_fixmap([(mp_fixstr("t"), mp_fixstr("battery")), (mp_fixstr("m"), bytes([0xCB]) + struct.pack(">d", 12.34))])
    assert ingest._msgpack_unpack(raw) == {"t": "battery", "m": 12.34}


def test_msgpack_unpack_fixarray(ingest):
    raw = mp_fixarray([bytes([1]), bytes([2]), mp_fixstr("x")])
    assert ingest._msgpack_unpack(raw) == [1, 2, "x"]


def test_msgpack_unpack_map16(ingest):
    entries = [(mp_fixstr("a"), bytes([1]))]
    raw = bytes([0xDE]) + struct.pack(">H", len(entries)) + b"".join(k + v for k, v in entries)
    assert ingest._msgpack_unpack(raw) == {"a": 1}


def test_msgpack_unpack_unsupported_byte_raises(ingest):
    with pytest.raises(ValueError):
        ingest._msgpack_unpack(bytes([0xC1]))


# ---- _decode_data_bin ----


_BIN_MAGIC = 0xC1  # ingest/index.pyの_BIN_MAGICと同じ値（テスト側で固定値として複製）


def _wrap_v1(body: bytes, *, version: int = 1, corrupt_crc: bool = False) -> bytes:
    header_and_body = bytes([_BIN_MAGIC, version]) + body
    crc = zlib.crc32(header_and_body)
    if corrupt_crc:
        crc ^= 0xFFFFFFFF
    return header_and_body + struct.pack("<I", crc)


def test_decode_data_bin_v1_valid(ingest):
    body = mp_fixmap([(mp_fixstr("t"), mp_fixstr("battery"))])
    raw = _wrap_v1(body)

    decoded, version = ingest._decode_data_bin(raw, "dev1")

    assert decoded == {"t": "battery"}
    assert version == 1
    assert _keys(ingest.CORRUPTED_BUCKET) == []


def test_decode_data_bin_v1_bad_crc_saves_corrupted(ingest):
    body = mp_fixmap([(mp_fixstr("t"), mp_fixstr("battery"))])
    raw = _wrap_v1(body, corrupt_crc=True)

    decoded, version = ingest._decode_data_bin(raw, "dev1")

    assert decoded is None and version is None
    assert len(_keys(ingest.CORRUPTED_BUCKET)) == 1


def test_decode_data_bin_unsupported_version_saves_corrupted(ingest):
    body = mp_fixmap([(mp_fixstr("t"), mp_fixstr("battery"))])
    raw = _wrap_v1(body, version=9)

    decoded, version = ingest._decode_data_bin(raw, "dev1")

    assert decoded is None and version is None
    assert len(_keys(ingest.CORRUPTED_BUCKET)) == 1


def test_decode_data_bin_v1_too_short_saves_corrupted(ingest):
    raw = bytes([0xC1, 1, 0, 0])  # magic+version+2バイトしかない（CRC分すら無い）

    decoded, version = ingest._decode_data_bin(raw, "dev1")

    assert decoded is None and version is None
    assert len(_keys(ingest.CORRUPTED_BUCKET)) == 1


def test_decode_data_bin_legacy_format_no_header(ingest):
    body = mp_fixmap([(mp_fixstr("t"), mp_fixstr("battery"))])  # 0x81始まり、magic 0xC1と衝突しない

    decoded, version = ingest._decode_data_bin(body, "dev1")

    assert decoded == {"t": "battery"}
    assert version == 0
    assert _keys(ingest.CORRUPTED_BUCKET) == []


def test_decode_data_bin_malformed_msgpack_saves_corrupted(ingest):
    body = bytes([0x81])  # fixmap宣言のみでkey/valueが続かない
    raw = _wrap_v1(body)

    decoded, version = ingest._decode_data_bin(raw, "dev1")

    assert decoded is None and version is None
    assert len(_keys(ingest.CORRUPTED_BUCKET)) == 1


# ---- handler: JSONトピック ----


def test_handler_json_topic_writes_partitioned_object(ingest):
    event = {"device_id": "dev1", "type": "battery", "main": 12.34, "ts": "2026-03-01T10:00:00Z"}

    ingest.handler(event, None)

    keys = _keys(ingest.BUCKET, "raw/year=2026/month=03/day=01/hour=10/")
    assert len(keys) == 1
    assert keys[0].startswith("raw/year=2026/month=03/day=01/hour=10/dev1-")
    payload = json.loads(_body(ingest.BUCKET, keys[0]))
    assert payload["device_id"] == "dev1"
    assert payload["type"] == "battery"
    assert payload["main"] == 12.34
    assert payload["ts"] == "2026-03-01T10:00:00Z"


def test_handler_expands_short_keys(ingest):
    event = {"device_id": "dev1", "t": "battery", "m": 1.1, "ts": "2026-03-01T10:00:00Z"}

    ingest.handler(event, None)

    key = _keys(ingest.BUCKET, "raw/")[0]
    payload = json.loads(_body(ingest.BUCKET, key))
    assert payload["type"] == "battery"
    assert payload["main"] == 1.1


def test_handler_skips_payload_without_type(ingest):
    event = {"device_id": "dev1", "ts": "2026-03-01T10:00:00Z"}

    ingest.handler(event, None)

    assert _keys(ingest.BUCKET) == []


def test_handler_ts_epoch_int_is_partitioned_correctly(ingest):
    epoch = int(datetime(2026, 3, 1, 10, 0, tzinfo=timezone.utc).timestamp())
    event = {"device_id": "dev1", "type": "battery", "ts": epoch}

    ingest.handler(event, None)

    assert _keys(ingest.BUCKET, "raw/year=2026/month=03/day=01/hour=10/") != []


def test_handler_invalid_ts_falls_back_to_now(ingest, monkeypatch):
    monkeypatch.setattr(ingest, "datetime", _FrozenDateTime)
    event = {"device_id": "dev1", "type": "battery", "ts": "not-a-timestamp"}

    ingest.handler(event, None)

    keys = _keys(ingest.BUCKET, "raw/year=2026/month=03/day=01/hour=10/")
    assert len(keys) == 1
    payload = json.loads(_body(ingest.BUCKET, keys[0]))
    assert payload["ts"] == "2026-03-01T10:00:00Z"


# ---- handler: data_binトピック ----


def test_handler_data_bin_new_format_writes_object(ingest):
    body = mp_fixmap([(mp_fixstr("t"), mp_fixstr("battery")), (mp_fixstr("ts"), mp_fixstr("2026-03-01T10:00:00Z"))])
    raw = _wrap_v1(body)
    event = {"device_id": "dev1", "payload": base64.b64encode(raw).decode()}

    ingest.handler(event, None)

    keys = _keys(ingest.BUCKET, "raw/year=2026/month=03/day=01/hour=10/")
    assert len(keys) == 1
    payload = json.loads(_body(ingest.BUCKET, keys[0]))
    assert payload["type"] == "battery"
    assert payload["ver"] == 1


def test_handler_data_bin_legacy_format_marks_ver_zero(ingest):
    body = mp_fixmap([(mp_fixstr("t"), mp_fixstr("battery")), (mp_fixstr("ts"), mp_fixstr("2026-03-01T10:00:00Z"))])
    event = {"device_id": "dev1", "payload": base64.b64encode(body).decode()}

    ingest.handler(event, None)

    keys = _keys(ingest.BUCKET, "raw/year=2026/month=03/day=01/hour=10/")
    payload = json.loads(_body(ingest.BUCKET, keys[0]))
    assert payload["ver"] == 0


def test_handler_data_bin_bad_crc_writes_nothing_to_raw(ingest):
    body = mp_fixmap([(mp_fixstr("t"), mp_fixstr("battery"))])
    raw = _wrap_v1(body, corrupt_crc=True)
    event = {"device_id": "dev1", "payload": base64.b64encode(raw).decode()}

    ingest.handler(event, None)

    assert _keys(ingest.BUCKET) == []
    assert len(_keys(ingest.CORRUPTED_BUCKET)) == 1
