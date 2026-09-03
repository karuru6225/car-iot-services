import io
import json
from decimal import Decimal

import pytest


def _event(method, proxy, body=None, is_admin=True):
    claims = {"cognito:groups": "[admin]" if is_admin else "[user]"}
    event = {
        "requestContext": {
            "authorizer": {"jwt": {"claims": claims}},
            "http": {"method": method},
        },
        "pathParameters": {"proxy": proxy},
    }
    if body is not None:
        event["body"] = body
    return event


# ---- _is_admin ----


def test_is_admin_true(admin):
    assert admin._is_admin(_event("GET", "devices", is_admin=True)) is True


def test_is_admin_false(admin):
    assert admin._is_admin(_event("GET", "devices", is_admin=False)) is False


# ---- handler: 権限・ルーティング ----


def test_handler_rejects_non_admin(admin):
    resp = admin.handler(_event("GET", "devices", is_admin=False), None)
    assert resp["statusCode"] == 403


def test_handler_unknown_route_is_404(admin):
    resp = admin.handler(_event("GET", "nope"), None)
    assert resp["statusCode"] == 404


def test_handler_wrong_method_for_route_is_404(admin):
    resp = admin.handler(_event("GET", "shadow/dev1"), None)
    assert resp["statusCode"] == 404


def test_handler_invalid_json_body_returns_500(admin):
    resp = admin.handler(_event("PUT", "shadow/dev1", body="not json"), None)
    assert resp["statusCode"] == 500


# ---- handle_devices ----


def test_handle_devices_lists_only_esp32_gw_prefix(admin):
    admin.iot.create_thing(thingName="esp32-gw-abc123")
    admin.iot.create_thing(thingName="other-thing")

    resp = admin.handler(_event("GET", "devices"), None)
    body = json.loads(resp["body"])

    device_ids = [d["device_id"] for d in body["devices"]]
    assert device_ids == ["esp32-gw-abc123"]


def test_handle_devices_reports_shadow_and_groups(admin, monkeypatch):
    # moto の iot-data は get/update_thing_shadow を実装していないため individual にmonkeypatchする
    admin.iot.create_thing(thingName="esp32-gw-abc123")
    admin.iot.create_thing_group(thingGroupName="ota-target-car-iot-gw-v1")
    admin.iot.add_thing_to_thing_group(thingName="esp32-gw-abc123", thingGroupName="ota-target-car-iot-gw-v1")

    shadow_doc = {"state": {"reported": {"fw_version": "1.20.0"}}, "timestamp": 1700000000}
    monkeypatch.setattr(
        admin.iot_data,
        "get_thing_shadow",
        lambda thingName: {"payload": io.BytesIO(json.dumps(shadow_doc).encode())},
    )

    resp = admin.handler(_event("GET", "devices"), None)
    body = json.loads(resp["body"])

    device = body["devices"][0]
    assert device["shadow"] == {"fw_version": "1.20.0"}
    assert device["last_seen_s"] == 1700000000
    assert device["groups"] == ["ota-target-car-iot-gw-v1"]
    assert "ota-target-car-iot-gw-v1" in body["all_groups"]


def test_handle_devices_tolerates_thing_without_shadow(admin, monkeypatch):
    admin.iot.create_thing(thingName="esp32-gw-no-shadow")

    def _raise(thingName):
        raise Exception("no shadow set")

    monkeypatch.setattr(admin.iot_data, "get_thing_shadow", _raise)

    resp = admin.handler(_event("GET", "devices"), None)
    body = json.loads(resp["body"])

    assert body["devices"][0]["shadow"] == {}
    assert body["devices"][0]["last_seen_s"] == 0


# ---- handle_shadow ----


def test_handle_shadow_updates_desired(admin, monkeypatch):
    admin.iot.create_thing(thingName="esp32-gw-abc123")
    captured = {}

    def _fake_update(thingName, payload):
        captured["thingName"] = thingName
        captured["payload"] = json.loads(payload)

    monkeypatch.setattr(admin.iot_data, "update_thing_shadow", _fake_update)

    resp = admin.handler(_event("PUT", "shadow/esp32-gw-abc123", body=json.dumps({"ah_offset": 50})), None)

    assert resp["statusCode"] == 200
    assert captured["thingName"] == "esp32-gw-abc123"
    assert captured["payload"] == {"state": {"desired": {"ah_offset": 50}}}


def test_handle_shadow_rejects_empty_body(admin):
    resp = admin.handler(_event("PUT", "shadow/esp32-gw-abc123", body=json.dumps({})), None)
    assert resp["statusCode"] == 400


# ---- handle_shadow_events ----


def test_handle_shadow_events_returns_recent_first(admin):
    admin.shadow_events_table.put_item(
        Item={"device_id": "esp32-gw-abc123", "ts": 1700000000, "changes": {"charging": {"from": False, "to": True}}}
    )
    admin.shadow_events_table.put_item(
        Item={"device_id": "esp32-gw-abc123", "ts": 1700000100, "changes": {"charging": {"from": True, "to": False}}}
    )

    resp = admin.handler(_event("GET", "shadow-events/esp32-gw-abc123"), None)
    body = json.loads(resp["body"])

    assert resp["statusCode"] == 200
    assert [e["ts"] for e in body["events"]] == [1700000100, 1700000000]


def test_handle_shadow_events_filters_by_device_id(admin):
    admin.shadow_events_table.put_item(
        Item={"device_id": "esp32-gw-abc123", "ts": 1700000000, "changes": {"charging": {"from": False, "to": True}}}
    )
    admin.shadow_events_table.put_item(
        Item={"device_id": "esp32-gw-other", "ts": 1700000000, "changes": {"charging": {"from": False, "to": True}}}
    )

    resp = admin.handler(_event("GET", "shadow-events/esp32-gw-abc123"), None)
    body = json.loads(resp["body"])

    assert [e["device_id"] for e in body["events"]] == ["esp32-gw-abc123"]


def test_handle_shadow_events_converts_decimal_for_json(admin):
    admin.shadow_events_table.put_item(
        Item={
            "device_id": "esp32-gw-abc123",
            "ts": 1700000000,
            "changes": {"chg_start_v": {"from": Decimal("11.7"), "to": Decimal("12.5")}},
        }
    )

    resp = admin.handler(_event("GET", "shadow-events/esp32-gw-abc123"), None)
    body = json.loads(resp["body"])

    assert body["events"][0]["changes"]["chg_start_v"] == {"from": 11.7, "to": 12.5}


def test_handle_shadow_events_empty_for_unknown_device(admin):
    resp = admin.handler(_event("GET", "shadow-events/esp32-gw-nobody"), None)
    body = json.loads(resp["body"])
    assert body["events"] == []


# ---- handle_command ----


@pytest.mark.parametrize("operation", ["ah_reset", "charge_start", "charge_stop"])
def test_handle_command_creates_job(admin, operation):
    resp = admin.handler(
        _event("POST", "command/esp32-gw-abc123", body=json.dumps({"operation": operation})), None
    )
    assert resp["statusCode"] == 200
    body = json.loads(resp["body"])
    assert body["job_id"].startswith("cmd-esp32-gw-abc123-")

    # レスポンスの形だけでなく、IoT側に実際にJobが作成されたことも確認する
    job = admin.iot.describe_job(jobId=body["job_id"])["job"]
    assert job["targets"] == [f"arn:aws:iot:{admin.REGION}:{admin.ACCOUNT_ID}:thing/esp32-gw-abc123"]
    document = admin.iot.get_job_document(jobId=body["job_id"])["document"]
    assert json.loads(document) == {"operation": operation}


def test_handle_command_rejects_unknown_operation(admin):
    resp = admin.handler(
        _event("POST", "command/esp32-gw-abc123", body=json.dumps({"operation": "reboot"})), None
    )
    assert resp["statusCode"] == 400


# ---- handle_groups ----


def test_handle_groups_adds_and_removes(admin):
    admin.iot.create_thing(thingName="esp32-gw-abc123")
    admin.iot.create_thing_group(thingGroupName="group-a")
    admin.iot.create_thing_group(thingGroupName="group-b")
    admin.iot.add_thing_to_thing_group(thingName="esp32-gw-abc123", thingGroupName="group-a")

    resp = admin.handler(
        _event("PUT", "groups/esp32-gw-abc123", body=json.dumps({"groups": ["group-b"]})), None
    )
    assert resp["statusCode"] == 200
    assert json.loads(resp["body"]) == {"groups": ["group-b"]}

    current = {
        g["groupName"]
        for g in admin.iot.list_thing_groups_for_thing(thingName="esp32-gw-abc123")["thingGroups"]
    }
    assert current == {"group-b"}
