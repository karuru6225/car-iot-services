import json
from dataclasses import replace
from datetime import datetime, timezone

import pytest

from carmcp import trips
from conftest import FakeGateway

NOW = datetime(2026, 9, 19, 3, 0, tzinfo=timezone.utc)
PREFIX = "trip-analysis/car-iot-aabbcc"


def _ts(y, mo, d, h, mi=0):
    return int(datetime(y, mo, d, h, mi, tzinfo=timezone.utc).timestamp())


def _trip(start, end, **fields):
    body = {
        "device_id": "car-iot-aabbcc", "session_start": start, "session_end": end, "row_count": 600,
        "start_location": "自宅", "end_location": "東京都千代田区丸の内付近",
        "distance_km": 23.4, "duration_sec": end - start, "fuel_l": 1.8, "fuel_economy_km_l": 13.0,
    }
    body.update(fields)
    return body


def _key(start, end, version=1, seq=1):
    dt = datetime.fromtimestamp(end, tz=timezone.utc)
    return f"{PREFIX}/year={dt.year:04d}/month={dt.month:02d}/{start:010d}_{end:010d}_v{version:02d}_{seq:03d}.json"


def _objects(*entries):
    return {key: json.dumps(body) for key, body in entries}


def test_car_trips_は期間内のトリップを出発時刻順に返す(cfg):
    a = (_ts(2026, 9, 16, 0), _ts(2026, 9, 16, 1, 2))
    b = (_ts(2026, 9, 17, 9), _ts(2026, 9, 17, 9, 30))
    outside = (_ts(2026, 9, 10, 0), _ts(2026, 9, 10, 1))
    gw = FakeGateway(objects=_objects(
        (_key(*b), _trip(*b)), (_key(*a), _trip(*a)), (_key(*outside), _trip(*outside)),
    ))
    result = trips.car_trips(gw, cfg, "2026-09-16", "2026-09-18", NOW)

    assert result["トリップ数"] == 2
    first = result["トリップ"][0]
    assert first["トリップID"] == f"{a[0]}_{a[1]}"
    assert first["出発時刻"] == "2026-09-16T09:00:00+09:00"
    assert first["所要時間"] == "1時間2分"
    assert first["走行距離"].startswith("23.4 km")
    assert first["燃費"] == "13.0 km/L"
    assert "出発地点" not in first  # 既定では地名を渡さない


def test_car_trips_はJSTの暦日で絞り月をまたぐトリップも拾う(cfg):
    # JST 9/1 00:30出発 = UTC 8/31 15:30。フォルダはsession_endの月（9月）
    t = (_ts(2026, 8, 31, 15, 30), _ts(2026, 8, 31, 16, 0))
    gw = FakeGateway(objects=_objects((_key(*t), _trip(*t))))
    assert trips.car_trips(gw, cfg, "2026-09-01", "2026-09-01", NOW)["トリップ数"] == 1
    assert trips.car_trips(gw, cfg, "2026-08-31", "2026-08-31", NOW)["トリップ数"] == 0


def test_car_trips_は同じ区間の最新の版だけを使う(cfg):
    t = (_ts(2026, 9, 16, 0), _ts(2026, 9, 16, 1))
    gw = FakeGateway(objects=_objects(
        (_key(*t, seq=1), _trip(*t, distance_km=1.0)),
        (_key(*t, seq=2), _trip(*t, distance_km=2.0)),
    ))
    result = trips.car_trips(gw, cfg, "2026-09-16", "2026-09-16", NOW)
    assert result["トリップ数"] == 1
    assert result["トリップ"][0]["走行距離"].startswith("2.0 km")


def test_car_trips_は許可されたときだけ地名を返す(cfg):
    t = (_ts(2026, 9, 16, 0), _ts(2026, 9, 16, 1))
    gw = FakeGateway(objects=_objects((_key(*t), _trip(*t))))
    trip = trips.car_trips(gw, replace(cfg, expose_location=True), "2026-09-16", "2026-09-16", NOW)["トリップ"][0]
    assert trip["出発地点"] == "自宅"
    assert trip["到着地点"] == "東京都千代田区丸の内付近"


@pytest.mark.parametrize(
    ("fields", "expected"),
    [
        ({"fuel_economy_km_l": 372.0, "distance_km": 10.0}, "物理的にありえない値"),
        ({"fuel_economy_km_l": 8.0, "distance_km": 0.4}, "参考にならない"),
        ({"fuel_economy_km_l": None, "fuel_l": None}, "算出できない"),
    ],
)
def test_car_trips_は燃費の妥当性を判定して渡す(cfg, fields, expected):
    t = (_ts(2026, 9, 16, 0), _ts(2026, 9, 16, 1))
    body = {k: v for k, v in _trip(*t, **fields).items() if v is not None}  # trip_analysisはNoneの項目を書かない
    gw = FakeGateway(objects=_objects((_key(*t), body)))
    trip = trips.car_trips(gw, cfg, "2026-09-16", "2026-09-16", NOW)["トリップ"][0]
    assert expected in trip["燃費"]


def test_car_trips_は記録の限界と未分析の範囲を注意として返す(cfg):
    t = (_ts(2026, 9, 16, 0), _ts(2026, 9, 16, 1))
    gw = FakeGateway(objects=_objects((_key(*t), _trip(*t))))
    notes = trips.car_trips(gw, cfg, "2026-09-16", "2026-09-19", NOW)["注意"]
    assert "走行しなかったことを意味しない" in notes[0]
    assert "2026-09-16T10:00:00+09:00 までの走行しか済んでいない" in notes[1]


def test_car_trips_は分析が一度も無いことを伝える(cfg):
    result = trips.car_trips(FakeGateway(), cfg, "2026-09-16", "2026-09-19", NOW)
    assert result["トリップ数"] == 0
    assert "一度も実行されていない" in result["注意"][1]


def test_car_trip_detail_は詳細の項目を意味付きで返す(cfg):
    t = (_ts(2026, 9, 16, 0), _ts(2026, 9, 16, 1))
    body = _trip(*t, ltft_avg=3.12, stft_avg=-1.5, catalyst_temp_max=612.4, boost_kpa_max=48.0,
                 coolant_start=35.0, coolant_end=88.0)
    gw = FakeGateway(objects=_objects((_key(*t), body)))
    detail = trips.car_trip_detail(gw, cfg, f"{t[0]}_{t[1]}")
    assert detail["長期燃料補正（LTFT）の平均"].startswith("+3.1 %")
    assert detail["短期燃料補正（STFT）の平均"].startswith("-1.5 %")
    assert detail["触媒温度の最高値"] == "612 ℃"
    assert detail["ブースト圧の最高値"].startswith("48 kPa")
    assert detail["冷却水温"] == "出発時 35 ℃ → 到着時 88 ℃"
    assert detail["OBDの記録件数"] == 600


def test_car_trip_detail_は不正なIDや存在しないトリップを拒否する(cfg):
    with pytest.raises(ValueError, match="トリップID"):
        trips.car_trip_detail(FakeGateway(), cfg, "../../etc")
    with pytest.raises(ValueError, match="見つからない"):
        trips.car_trip_detail(FakeGateway(), cfg, "1789500000_1789503600")
