from decimal import Decimal


def _get_item(module, device_id, ts):
    resp = module.table.get_item(Key={"device_id": device_id, "ts": ts})
    return resp.get("Item")


# ---- _is_valid ----


def test_is_valid_accepts_known_type(shadow_events):
    assert shadow_events._is_valid("charging", True) is True


def test_is_valid_rejects_none(shadow_events):
    assert shadow_events._is_valid("charging", None) is False


def test_is_valid_rejects_unknown_key(shadow_events):
    assert shadow_events._is_valid("unknown_field", "garbled") is False


def test_is_valid_rejects_type_mismatch(shadow_events):
    assert shadow_events._is_valid("ah_offset", "not-a-number") is False


def test_is_valid_rejects_bool_masquerading_as_int(shadow_events):
    assert shadow_events._is_valid("ah_offset", True) is False


def test_is_valid_accepts_allowed_override_mode(shadow_events):
    assert shadow_events._is_valid("override_next_mode", "timed_continuous") is True


def test_is_valid_rejects_disallowed_override_mode(shadow_events):
    assert shadow_events._is_valid("override_next_mode", "not_allowed") is False


def test_is_valid_accepts_allowed_default_mode(shadow_events):
    assert shadow_events._is_valid("default_mode", "light_sleep") is True


def test_is_valid_rejects_disallowed_default_mode(shadow_events):
    assert shadow_events._is_valid("default_mode", "not_allowed") is False


# ---- handler ----


def test_handler_records_simple_change(shadow_events):
    event = {
        "device_id": "dev1",
        "reported": {"charging": True},
        "previous_reported": {"charging": False},
        "ts": 1700000000,
    }

    shadow_events.handler(event, None)

    item = _get_item(shadow_events, "dev1", 1700000000)
    assert item["changes"] == {"charging": {"from": False, "to": True}}


def test_handler_ignores_no_diff(shadow_events):
    event = {
        "device_id": "dev1",
        "reported": {"charging": True},
        "previous_reported": {"charging": True},
        "ts": 1700000000,
    }

    shadow_events.handler(event, None)

    assert _get_item(shadow_events, "dev1", 1700000000) is None


def test_handler_converts_float_to_decimal(shadow_events):
    event = {
        "device_id": "dev1",
        "reported": {"chg_start_v": 11.8},
        "previous_reported": {"chg_start_v": 11.7},
        "ts": 1700000000,
    }

    shadow_events.handler(event, None)

    item = _get_item(shadow_events, "dev1", 1700000000)
    assert item["changes"]["chg_start_v"]["to"] == Decimal("11.8")
    assert item["changes"]["chg_start_v"]["from"] == Decimal("11.7")


def test_handler_records_only_changed_keys(shadow_events):
    event = {
        "device_id": "dev1",
        "reported": {"charging": True, "ah_offset": 50},
        "previous_reported": {"charging": False, "ah_offset": 50},
        "ts": 1700000000,
    }

    shadow_events.handler(event, None)

    item = _get_item(shadow_events, "dev1", 1700000000)
    assert item["changes"] == {"charging": {"from": False, "to": True}}


def test_handler_skips_unknown_key_noise(shadow_events):
    event = {
        "device_id": "dev1",
        "reported": {"unknown_field": "garbled"},
        "previous_reported": {},
        "ts": 1700000000,
    }

    shadow_events.handler(event, None)

    assert _get_item(shadow_events, "dev1", 1700000000) is None


def test_handler_skips_type_mismatch_noise(shadow_events):
    event = {
        "device_id": "dev1",
        "reported": {"ah_offset": "not-a-number"},
        "previous_reported": {"ah_offset": 50},
        "ts": 1700000000,
    }

    shadow_events.handler(event, None)

    assert _get_item(shadow_events, "dev1", 1700000000) is None


def test_handler_skips_disallowed_override_mode_noise(shadow_events):
    event = {
        "device_id": "dev1",
        "reported": {"override_next_mode": "not_allowed"},
        "previous_reported": {},
        "ts": 1700000000,
    }

    shadow_events.handler(event, None)

    assert _get_item(shadow_events, "dev1", 1700000000) is None


def test_handler_skips_shadow_guard_correction_to_null(shadow_events):
    # shadow_guardがノイズを補正してキーがreportedから消えたケース
    # （current.state.reportedにキー自体が存在しなくなる = event["reported"]に含まれない）
    event = {
        "device_id": "dev1",
        "reported": {},
        "previous_reported": {"unknown_field": "garbled"},
        "ts": 1700000000,
    }

    shadow_events.handler(event, None)

    assert _get_item(shadow_events, "dev1", 1700000000) is None


def test_handler_skips_when_all_changes_filtered_out(shadow_events):
    event = {
        "device_id": "dev1",
        "reported": {"unknown_a": 1, "ah_offset": "bad"},
        "previous_reported": {},
        "ts": 1700000000,
    }

    shadow_events.handler(event, None)

    assert _get_item(shadow_events, "dev1", 1700000000) is None
