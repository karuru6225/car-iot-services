import json


def _capture_updates(monkeypatch, module):
    calls = []
    monkeypatch.setattr(
        module.iot_data,
        "update_thing_shadow",
        lambda thingName, payload: calls.append((thingName, json.loads(payload))),
    )
    return calls


# ---- _type_ok ----


def test_type_ok_accepts_matching_type(shadow_guard):
    assert shadow_guard._type_ok(int, 5) is True


def test_type_ok_rejects_bool_for_int_field(shadow_guard):
    # bool は int のサブクラスなので、素の isinstance だけだと ah_offset: true をすり抜けてしまう
    assert shadow_guard._type_ok(int, True) is False


def test_type_ok_accepts_bool_for_bool_field(shadow_guard):
    assert shadow_guard._type_ok(bool, True) is True


def test_type_ok_rejects_int_for_bool_field(shadow_guard):
    assert shadow_guard._type_ok(bool, 1) is False


def test_type_ok_accepts_either_of_tuple(shadow_guard):
    assert shadow_guard._type_ok((int, float), 1.5) is True
    assert shadow_guard._type_ok((int, float), 1) is True


# ---- handler ----


def test_handler_ignores_non_dict_reported(shadow_guard, monkeypatch):
    calls = _capture_updates(monkeypatch, shadow_guard)
    shadow_guard.handler({"device_id": "dev1", "reported": "not-a-dict"}, None)
    assert calls == []


def test_handler_no_corrections_when_all_valid(shadow_guard, monkeypatch):
    calls = _capture_updates(monkeypatch, shadow_guard)
    event = {"device_id": "dev1", "reported": {"ah_offset": 50, "charging": True}}

    shadow_guard.handler(event, None)

    assert calls == []


def test_handler_nulls_unknown_key(shadow_guard, monkeypatch):
    calls = _capture_updates(monkeypatch, shadow_guard)
    event = {"device_id": "dev1", "reported": {"unknown_field": "garbled"}}

    shadow_guard.handler(event, None)

    assert calls == [("dev1", {"state": {"reported": {"unknown_field": None}}})]


def test_handler_nulls_type_mismatch(shadow_guard, monkeypatch):
    calls = _capture_updates(monkeypatch, shadow_guard)
    event = {"device_id": "dev1", "reported": {"ah_offset": "not-a-number"}}

    shadow_guard.handler(event, None)

    assert calls == [("dev1", {"state": {"reported": {"ah_offset": None}}})]


def test_handler_nulls_bool_masquerading_as_int(shadow_guard, monkeypatch):
    calls = _capture_updates(monkeypatch, shadow_guard)
    event = {"device_id": "dev1", "reported": {"ah_offset": True}}

    shadow_guard.handler(event, None)

    assert calls == [("dev1", {"state": {"reported": {"ah_offset": None}}})]


def test_handler_accepts_allowed_override_mode(shadow_guard, monkeypatch):
    calls = _capture_updates(monkeypatch, shadow_guard)
    event = {"device_id": "dev1", "reported": {"override_next_mode": "one_shot_continuous"}}

    shadow_guard.handler(event, None)

    assert calls == []


def test_handler_nulls_disallowed_override_mode(shadow_guard, monkeypatch):
    calls = _capture_updates(monkeypatch, shadow_guard)
    event = {"device_id": "dev1", "reported": {"override_next_mode": "not_allowed"}}

    shadow_guard.handler(event, None)

    assert calls == [("dev1", {"state": {"reported": {"override_next_mode": None}}})]


def test_handler_skips_null_values(shadow_guard, monkeypatch):
    calls = _capture_updates(monkeypatch, shadow_guard)
    event = {"device_id": "dev1", "reported": {"ah_offset": None, "totally_unknown": None}}

    shadow_guard.handler(event, None)

    assert calls == []


def test_handler_batches_multiple_corrections(shadow_guard, monkeypatch):
    calls = _capture_updates(monkeypatch, shadow_guard)
    event = {"device_id": "dev1", "reported": {"unknown_a": 1, "ah_offset": "bad", "charging": True}}

    shadow_guard.handler(event, None)

    assert len(calls) == 1
    thing_name, payload = calls[0]
    assert thing_name == "dev1"
    corrections = payload["state"]["reported"]
    assert corrections == {"unknown_a": None, "ah_offset": None}


def test_handler_swallows_update_errors(shadow_guard, monkeypatch):
    def _boom(thingName, payload):
        raise RuntimeError("shadow update failed")

    monkeypatch.setattr(shadow_guard.iot_data, "update_thing_shadow", _boom)
    event = {"device_id": "dev1", "reported": {"unknown_field": 1}}

    shadow_guard.handler(event, None)  # 例外が外に漏れなければOK
