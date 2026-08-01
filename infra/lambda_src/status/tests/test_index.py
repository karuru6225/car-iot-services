import io
import json
from datetime import datetime, timezone


class _FrozenDateTime(datetime):
    @classmethod
    def now(cls, tz=None):
        return datetime(2026, 3, 1, 10, 10, 0, tzinfo=tz)


_FROZEN_NOW_S = _FrozenDateTime.now(timezone.utc).timestamp()


def _fake_shadow(monkeypatch, module, *, reported=None, timestamp=0):
    doc = {"state": {"reported": reported or {}}, "timestamp": timestamp}
    monkeypatch.setattr(
        module.iot,
        "get_thing_shadow",
        lambda thingName: {"payload": io.BytesIO(json.dumps(doc).encode())},
    )


def test_handler_alive_within_threshold(status, monkeypatch):
    monkeypatch.setattr(status, "datetime", _FrozenDateTime)
    _fake_shadow(monkeypatch, status, reported={"sub_v": 13.1, "main_v": 12.4}, timestamp=int(_FROZEN_NOW_S) - 60)

    resp = status.handler({}, None)
    body = json.loads(resp["body"])

    assert body["alive"] is True
    assert body["age_s"] == 60
    assert body["sub_v"] == 13.1
    assert body["main_v"] == 12.4


def test_handler_not_alive_beyond_threshold(status, monkeypatch):
    monkeypatch.setattr(status, "datetime", _FrozenDateTime)
    _fake_shadow(monkeypatch, status, reported={}, timestamp=int(_FROZEN_NOW_S) - 601)

    resp = status.handler({}, None)
    body = json.loads(resp["body"])

    assert body["alive"] is False
    assert body["age_s"] == 601


def test_handler_defaults_missing_voltages_to_zero(status, monkeypatch):
    monkeypatch.setattr(status, "datetime", _FrozenDateTime)
    _fake_shadow(monkeypatch, status, reported={}, timestamp=int(_FROZEN_NOW_S))

    resp = status.handler({}, None)
    body = json.loads(resp["body"])

    assert body["sub_v"] == 0.0
    assert body["main_v"] == 0.0


def test_handler_coerces_voltages_to_float(status, monkeypatch):
    monkeypatch.setattr(status, "datetime", _FrozenDateTime)
    _fake_shadow(monkeypatch, status, reported={"sub_v": 13, "main_v": 12}, timestamp=int(_FROZEN_NOW_S))

    resp = status.handler({}, None)
    body = json.loads(resp["body"])

    assert body["sub_v"] == 13.0
    assert body["main_v"] == 12.0


def test_handler_returns_not_alive_when_shadow_missing(status, monkeypatch):
    def _raise(thingName):
        raise status.iot.exceptions.ResourceNotFoundException(
            {"Error": {"Code": "ResourceNotFoundException", "Message": "No shadow exists"}},
            "GetThingShadow",
        )

    monkeypatch.setattr(status.iot, "get_thing_shadow", _raise)

    resp = status.handler({}, None)
    body = json.loads(resp["body"])

    assert resp["statusCode"] == 200
    assert body == {"device_id": status.THING_NAME, "alive": False, "error": "shadow not found"}
