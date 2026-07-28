"""
IoT Core Topic Rule → Lambda → S3

受け取るペイロード例（JSON トピック sensors/+/data）:
  {"device_id":"m5stamp-lite-01", "t":"battery", "m":12.34, ...}

受け取るペイロード例（MsgPack トピック sensors/+/data_bin）:
  {"device_id":"m5stamp-lite-01", "payload":"<base64>"}
  ※ IoT Rule の encode(*,'base64') で base64 化されて届く

  payload のバイナリ形式（ESP32↔SIM7080G間のUARTにパリティ・CRCがなく、稀に1ビット
  化けがそのままクラウドまで届く問題への対策。詳細はesp32_iot_gateway/CONTEXT.md参照）:
    新形式: [magic 0xC1][version][msgpack本体][CRC32 4B(LE, magic~本体まで)]
    旧形式: msgpack本体のみ（ヘッダ・CRC無し）
    magic 0xC1 は MessagePack 仕様で「未使用」と規定されたバイト値で、旧形式の本体
    先頭（fixmap: 0x80-0x8F）と衝突しないため、ファーム/Lambdaどちらが先にデプロイ
    されても両形式を自動判別できる（デプロイ順序に依存しない）

保存先:
  s3://{BUCKET}/raw/year=YYYY/month=MM/day=DD/hour=HH/{device_id}-{uuid8}.json
  CRC不一致時: s3://{CORRUPTED_BUCKET}/year=/month=/day=/hour=/{device_id}-{uuid8}.bin
    （生バイナリのまま。raw/ 側には保存しない＝Athenaスキーマは無傷）
"""

import base64
import json
import os
import struct
import uuid
import zlib
from datetime import datetime, timezone

import boto3

s3 = boto3.client("s3")
BUCKET = os.environ["S3_BUCKET"]
CORRUPTED_BUCKET = os.environ["CORRUPTED_BUCKET"]

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


_BIN_MAGIC = 0xC1  # MessagePack仕様で「未使用」と規定されたバイト値


def _save_corrupted(raw: bytes, device_id: str, reason: str):
    now = datetime.now(timezone.utc)
    key = (
        f"year={now.strftime('%Y')}/"
        f"month={now.strftime('%m')}/"
        f"day={now.strftime('%d')}/"
        f"hour={now.strftime('%H')}/"
        f"{device_id}-{uuid.uuid4().hex[:8]}.bin"
    )
    print(f"[CORRUPT] {reason} device_id={device_id} len={len(raw)} raw={raw.hex()}")
    try:
        s3.put_object(
            Bucket=CORRUPTED_BUCKET,
            Key=key,
            Body=raw,
            ContentType="application/octet-stream",
        )
        print(f"[CORRUPT] saved s3://{CORRUPTED_BUCKET}/{key}")
    except Exception as e:
        # 診断用の退避なので失敗しても ingest 本流は止めない
        print(f"[ERROR] failed to save corrupted payload: {e}")


def _decode_data_bin(raw: bytes, device_id: str):
    """data_bin ペイロード（生バイナリ）をデコードする。
    新形式（magic 0xC1 始まり）と旧形式（ヘッダ無し）を自動判別する。
    戻り値: (デコード済み dict, pkt_version) のタプル。
      pkt_version は 0=旧形式（ヘッダ無し）、1=新形式(magic+version+CRC)。
      破損時は (None, None)（破損として処理済み・呼び出し元は何もしなくてよい）
    """
    if len(raw) >= 2 and raw[0] == _BIN_MAGIC:
        version = raw[1]
        if version != 1:
            _save_corrupted(raw, device_id, f"unsupported bin format version={version}")
            return None, None
        if len(raw) < 2 + 4:
            _save_corrupted(raw, device_id, f"v1 payload too short ({len(raw)} bytes)")
            return None, None

        header_and_body = raw[:-4]
        expected_crc = struct.unpack("<I", raw[-4:])[0]
        actual_crc = zlib.crc32(header_and_body)
        if actual_crc != expected_crc:
            _save_corrupted(raw, device_id, f"CRC mismatch (expected=0x{expected_crc:08X}, actual=0x{actual_crc:08X})")
            return None, None
        body = header_and_body[2:]
        pkt_version = version
    else:
        # 旧形式（マジックバイト無し・ファーム未更新のデバイス向け後方互換）: CRC検証なしでそのままデコード
        body = raw
        pkt_version = 0

    try:
        decoded = _msgpack_unpack(body)
    except Exception as e:
        _save_corrupted(raw, device_id, f"msgpack decode failed: {e}")
        return None, None
    return decoded, pkt_version


def handler(event, context):
    device_id = event.get("device_id", "unknown")

    # data_bin トピック経由（IoT Rule が encode(*,'base64') して payload キーに格納）
    if "payload" in event:
        raw = base64.b64decode(event["payload"])
        decoded, pkt_version = _decode_data_bin(raw, device_id)
        if decoded is None:
            return
        print(f"[OK] device_id={device_id} ver={pkt_version}")
        decoded["device_id"] = device_id
        decoded["ver"] = pkt_version
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
