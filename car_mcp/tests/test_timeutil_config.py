from datetime import date, datetime, timezone

import pytest

from carmcp.config import derive_obd_device_id, load_config
from carmcp.timeutil import describe_age, describe_duration, iso_jst, jst_day_start, validate_period

TODAY = date(2026, 9, 19)


def test_iso_jst_は時差付きの日本時間になる():
    assert iso_jst(datetime(2026, 9, 18, 15, 30, tzinfo=timezone.utc)) == "2026-09-19T00:30:00+09:00"
    assert iso_jst(0) == "1970-01-01T09:00:00+09:00"


def test_jst_day_start_はUTCの前日15時():
    assert jst_day_start(date(2026, 9, 19)) == datetime(2026, 9, 18, 15, tzinfo=timezone.utc)


def test_validate_period_は未来の終了日を今日に丸める():
    assert validate_period("2026-09-10", "2026-12-31", TODAY, 92) == (date(2026, 9, 10), TODAY)


@pytest.mark.parametrize(
    ("start", "end", "message"),
    [
        ("2026/09/10", "2026-09-11", "YYYY-MM-DD"),
        ("2026-02-30", "2026-03-01", "存在しない日付"),
        ("2026-09-12", "2026-09-11", "より前"),
        ("2026-09-20", "2026-09-21", "未来"),
        ("2026-01-01", "2026-09-19", "最大92日"),
    ],
)
def test_validate_period_は誤った期間を理由付きで拒否する(start, end, message):
    with pytest.raises(ValueError, match=message):
        validate_period(start, end, TODAY, 92)


def test_describe_age_と_describe_duration():
    assert describe_age(30) == "30秒前"
    assert describe_age(12 * 60 + 5) == "約12分前"
    assert describe_age(3 * 3600 + 120) == "約3時間2分前"
    assert describe_age(5 * 86400) == "約5日前"
    assert describe_duration(3720) == "1時間2分"
    assert describe_duration(45) == "45秒"


def test_derive_obd_device_id_はMAC上位6桁を使う():
    assert derive_obd_device_id("esp32-gw-aabbccddeeff") == "car-iot-aabbcc"


def _env(**overrides):
    env = {
        "CAR_DEVICE_ID": "esp32-gw-aabbccddeeff",
        "CAR_S3_BUCKET": "bucket",
        "CAR_IOT_ENDPOINT": "example-ats.iot.ap-northeast-1.amazonaws.com",
        "CAR_MCP_TOKEN": "x" * 32,
    }
    env.update(overrides)
    return {k: v for k, v in env.items() if v is not None}


def test_load_config_の既定値():
    cfg = load_config(_env())
    assert cfg.obd_device_id == "car-iot-aabbcc"
    assert cfg.iot_endpoint == "https://example-ats.iot.ap-northeast-1.amazonaws.com"
    assert cfg.athena_database == "iot_monitor"
    assert cfg.athena_workgroup == "iot-monitor"
    assert cfg.expose_location is False
    assert "car-mcp:*" in cfg.allowed_hosts


def test_load_config_は地名の公開を明示したときだけ有効にする():
    assert load_config(_env(CAR_EXPOSE_LOCATION="true")).expose_location is True
    assert load_config(_env(CAR_EXPOSE_LOCATION="yes")).expose_location is False


@pytest.mark.parametrize(
    ("overrides", "message"),
    [
        ({"CAR_MCP_TOKEN": None}, "CAR_MCP_TOKEN"),
        ({"CAR_MCP_TOKEN": "short"}, "32文字以上"),
        ({"CAR_DEVICE_ID": "esp32-gw-x' OR '1'='1"}, "使えない文字"),
        ({"CAR_S3_BUCKET": ""}, "CAR_S3_BUCKET"),
    ],
)
def test_load_config_は不備があれば起動させない(overrides, message):
    with pytest.raises(RuntimeError, match=message):
        load_config(_env(**overrides))
