"""
IoT Core Topic Rule（$aws/things/+/shadow/update/accepted） → Lambda

ESP32↔SIM7080G間のUARTにパリティ・CRCが無く、稀な1ビット化けが
$aws/things/{id}/shadow/update にそのまま乗って reported にマージされる問題への対策
（詳細は esp32_iot_gateway/CONTEXT.md 参照）。

data_bin と異なり Device Shadow はマージ前に検証を挟めない（予約トピックのため
Topic Rule で publish 前に横取りできない）ので、事後検知 + 即時修正で対応する。
update/accepted はその回の更新で変化したキーのみを state.reported に含むため、
device_id ごとに whitelist（許可キー名 + 型）と照合し、範囲外のキーや型不一致を
検知したら null で上書きして reported から除去する。

注意: 値そのものの化け（許可キーのまま値だけ壊れたケース、例: chg_start_v が
妥当な型のまま桁が飛ぶ）までは検出できない。あくまでキー名の化けによる
未知キーの蓄積を防ぐための対策。

telemetry.cpp の buildConfigPayload が送る reported フィールドを追加・変更したら
このファイルの SCHEMA も合わせて更新すること。
"""

import json
import os

import boto3

IOT_ENDPOINT = os.environ["IOT_ENDPOINT"]

iot_data = boto3.client("iot-data", endpoint_url=IOT_ENDPOINT)

ALLOWED_OVERRIDE_MODES = {"timed_continuous"}

# キー名 → 期待する型（tuple の場合はいずれかに一致すればよい）
SCHEMA = {
    "ah_offset": int,
    "chg_start_v": (int, float),
    "chg_stop_v": (int, float),
    "chg_min_diff_v": (int, float),
    "debug_log": bool,
    "charging": bool,
    "override_next_mode": (str, type(None)),
    "fw_version": str,
}


def _type_ok(expected, value):
    if isinstance(value, bool):
        return expected is bool
    return isinstance(value, expected)


def handler(event, context):
    device_id = event.get("device_id", "unknown")
    reported = event.get("reported")
    if not isinstance(reported, dict):
        return

    corrections = {}
    for key, value in reported.items():
        # null は「未設定/削除済み」であり検証対象外。
        # 自分自身が発行した修正（null 上書き）を再チェックして無限ループするのも防ぐ。
        if value is None:
            continue

        if key not in SCHEMA:
            print(f"[GUARD] unknown key device_id={device_id} key={key!r} value={value!r}")
            corrections[key] = None
            continue

        expected = SCHEMA[key]
        if not _type_ok(expected, value):
            print(f"[GUARD] type mismatch device_id={device_id} key={key} value={value!r} expected={expected}")
            corrections[key] = None
            continue

        if key == "override_next_mode" and value not in ALLOWED_OVERRIDE_MODES:
            print(f"[GUARD] invalid override_next_mode device_id={device_id} value={value!r}")
            corrections[key] = None

    if not corrections:
        return

    payload = json.dumps({"state": {"reported": corrections}})
    try:
        iot_data.update_thing_shadow(thingName=device_id, payload=payload.encode())
        print(f"[GUARD] corrected device_id={device_id} keys={list(corrections)}")
    except Exception as e:
        print(f"[ERROR] failed to correct shadow device_id={device_id}: {e}")
