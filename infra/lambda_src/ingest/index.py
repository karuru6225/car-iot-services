"""
IoT Core Topic Rule → Lambda → S3

受け取るペイロード例（JSON トピック sensors/+/data）:
  {"device_id":"m5stamp-lite-01", "t":"battery", "m":12.34, ...}

受け取るペイロード例（MsgPack トピック sensors/+/data_bin）:
  {"device_id":"m5stamp-lite-01", "payload":"<base64>"}
  ※ IoT Rule の encode(*,'base64') で base64 化されて届く

保存先:
  s3://{BUCKET}/raw/year=YYYY/month=MM/day=DD/hour=HH/{device_id}-{uuid8}.json
"""

import base64
import json
import os
import struct
import uuid
from datetime import datetime, timezone

import boto3

s3 = boto3.client("s3")
BUCKET = os.environ["S3_BUCKET"]

_SHORT_TO_FULL = {
    "t": "type", "m": "main", "s": "sub", "i": "current",
    "p": "power", "tp": "temp", "a": "addr", "h": "humidity",
    "bt": "battery", "rs": "rssi",
}


def _expand_keys(d: dict) -> dict:
    return {_SHORT_TO_FULL.get(k, k): v for k, v in d.items()}


# ─── インライン MessagePack デコーダ（外部依存なし）────────────────────────────
# ArduinoJson v7 の serializeMsgPack が出力する範囲のみサポート。
# fixmap / fixstr / fixint / float32 / float64 / uint8-64 / int8-32 / bool / nil

def _msgpack_unpack(data: bytes):
    pos = [0]

    def read(n: int) -> bytes:
        chunk = data[pos[0]:pos[0] + n]
        pos[0] += n
        return chunk

    def decode_one():
        b = data[pos[0]]
        pos[0] += 1

        if b >> 7 == 0:           # positive fixint
            return b
        if b >> 5 == 0b111:       # negative fixint
            return struct.unpack("b", bytes([b]))[0]
        if b >> 5 == 0b101:       # fixstr
            n = b & 0x1F
            return read(n).decode()
        if b >> 4 == 0b1000:      # fixmap
            n = b & 0x0F
            return {decode_one(): decode_one() for _ in range(n)}
        if b >> 4 == 0b1001:      # fixarray
            n = b & 0x0F
            return [decode_one() for _ in range(n)]

        if b == 0xC0: return None
        if b == 0xC2: return False
        if b == 0xC3: return True
        if b == 0xCA: return struct.unpack(">f", read(4))[0]
        if b == 0xCB: return struct.unpack(">d", read(8))[0]
        if b == 0xCC: return struct.unpack("B", read(1))[0]
        if b == 0xCD: return struct.unpack(">H", read(2))[0]
        if b == 0xCE: return struct.unpack(">I", read(4))[0]
        if b == 0xCF: return struct.unpack(">Q", read(8))[0]
        if b == 0xD0: return struct.unpack("b", read(1))[0]
        if b == 0xD1: return struct.unpack(">h", read(2))[0]
        if b == 0xD2: return struct.unpack(">i", read(4))[0]
        if b == 0xD9:
            n = struct.unpack("B", read(1))[0]
            return read(n).decode()
        if b == 0xDA:
            n = struct.unpack(">H", read(2))[0]
            return read(n).decode()
        if b == 0xDE:
            n = struct.unpack(">H", read(2))[0]
            return {decode_one(): decode_one() for _ in range(n)}
        if b == 0xDF:
            n = struct.unpack(">I", read(4))[0]
            return {decode_one(): decode_one() for _ in range(n)}

        raise ValueError(f"unsupported msgpack byte: 0x{b:02X} at pos {pos[0]-1}")

    return decode_one()


def handler(event, context):
    device_id = event.get("device_id", "unknown")

    # data_bin トピック経由（IoT Rule が encode(*,'base64') して payload キーに格納）
    if "payload" in event:
        raw = base64.b64decode(event["payload"])
        try:
            decoded = _msgpack_unpack(raw)
        except Exception as e:
            print(f"[ERROR] msgpack decode failed: {e}, raw={raw.hex()}")
            raise
        decoded["device_id"] = device_id
        event = decoded

    event = _expand_keys(event)
    device_id = event.get("device_id", "unknown")

    ts_raw = event.get("ts", "")
    if ts_raw:
        try:
            if isinstance(ts_raw, (int, float)):
                dt = datetime.fromtimestamp(int(ts_raw), tz=timezone.utc)
            else:
                dt = datetime.strptime(str(ts_raw), "%Y-%m-%dT%H:%M:%SZ").replace(tzinfo=timezone.utc)
        except (ValueError, OSError):
            dt = datetime.now(timezone.utc)
    else:
        dt = datetime.now(timezone.utc)

    if "type" not in event:
        print(f"[SKIP] 認識できないペイロード: {json.dumps(event)}")
        return

    key = (
        f"raw/"
        f"year={dt.strftime('%Y')}/"
        f"month={dt.strftime('%m')}/"
        f"day={dt.strftime('%d')}/"
        f"hour={dt.strftime('%H')}/"
        f"{device_id}-{uuid.uuid4().hex[:8]}.json"
    )

    payload = {
        **event,
        "device_id": device_id,
        "ts": dt.strftime("%Y-%m-%dT%H:%M:%SZ"),
    }

    try:
        s3.put_object(
            Bucket=BUCKET,
            Key=key,
            Body=json.dumps(payload),
            ContentType="application/json",
        )
        print(f"[OK] s3://{BUCKET}/{key}")
    except Exception as e:
        print(f"[ERROR] {e}")
        raise
