"""
IoT Core Topic Rule（$aws/things/+/shadow/update/documents） → Lambda

Shadow の reported フィールドが実際に変化した瞬間だけを DynamoDB に記録する。
update/documents は current/previous 両方の完全な reported ドキュメントを含み、
内容が変わらない更新では AWS IoT 側で発行自体がスキップされるため、このトピックへの
到着＝「reported のどこかが変わった」を意味する。どのキーが変わったかの差分計算は
IoT SQL では表現できないため、ここで previous/current を比較して行う。

shadow_guard（同ディレクトリ内 ../shadow_guard/index.py）が対策している
ESP32↔SIM7080G間のUART化けは、このイベントログにも同じ形でノイズとして混入しうる:

  1. 化けた値そのもの（未知キーの出現・型不一致の値）
  2. shadow_guard がそれを補正した際の「キーが reported から消える（null化）」イベント
     （実際のデバイス状態変化ではなく補正の副作用）

そのため shadow_guard と同一の SCHEMA で「変化後の値」を検証し、妥当なものだけを記録する。
telemetry.cpp の buildConfigPayload が送る reported フィールドを追加・変更したら
shadow_guard/index.py の SCHEMA と合わせてこちらも更新すること。
"""

import os
from decimal import Decimal

import boto3

TABLE_NAME = os.environ["TABLE_NAME"]

dynamodb = boto3.resource("dynamodb")
table = dynamodb.Table(TABLE_NAME)

ALLOWED_OVERRIDE_MODES = {"timed_continuous"}
ALLOWED_DEFAULT_MODES = {"deep_sleep", "continuous", "light_sleep"}

SCHEMA = {
    "ah_offset": int,
    "chg_start_v": (int, float),
    "chg_stop_v": (int, float),
    "chg_min_diff_v": (int, float),
    "debug_log": bool,
    "charging": bool,
    "override_next_mode": (str, type(None)),
    "continuous_until_time": (int, type(None)),
    "default_mode": (str, type(None)),
    "fw_version": str,
}


def _type_ok(expected, value):
    if isinstance(value, bool):
        return expected is bool
    return isinstance(value, expected)


def _is_valid(key, value):
    # null は shadow_guard によるキー削除の補正、またはノイズそのものであり記録対象外
    if value is None:
        return False
    if key not in SCHEMA or not _type_ok(SCHEMA[key], value):
        return False
    if key == "override_next_mode" and value not in ALLOWED_OVERRIDE_MODES:
        return False
    if key == "default_mode" and value not in ALLOWED_DEFAULT_MODES:
        return False
    return True


def _to_dynamo(value):
    # boto3 の Table リソースは float を受け付けないため Decimal 化する
    if isinstance(value, float):
        return Decimal(str(value))
    if isinstance(value, dict):
        return {k: _to_dynamo(v) for k, v in value.items()}
    return value


def handler(event, context):
    device_id = event.get("device_id", "unknown")
    current = event.get("reported") or {}
    previous = event.get("previous_reported") or {}
    ts = event.get("ts")

    changes = {}
    for key, value in current.items():
        if previous.get(key) == value or not _is_valid(key, value):
            continue
        changes[key] = {"from": previous.get(key), "to": value}

    if not changes:
        return

    table.put_item(Item={
        "device_id": device_id,
        "ts": ts,
        "changes": _to_dynamo(changes),
    })
