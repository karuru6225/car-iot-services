import json
from datetime import datetime, timezone

import pytest

from carmcp import battery
from car_mcp_fakes import FakeGateway

NOW = datetime(2026, 9, 19, 3, 0, tzinfo=timezone.utc)  # JST 12:00


def _row(ts, main=12.45, sub=13.1, current=-0.07, power=-0.9, temp=31.2, ah=210.5):
    return {"ts": ts, "main": main, "sub": sub, "current": current, "power": power, "temp": temp, "ah": ah}


SHADOW = {
    "state": {"reported": {"charging": False, "chg_start_v": 11.7, "chg_stop_v": 12.5, "fw_version": "1.25.0+abc"}},
    "timestamp": 1789700000,
}


def test_car_status_は最新の計測を単位と意味付きで返す(cfg):
    gw = FakeGateway(query_results=[[_row("2026-09-19T02:55:00Z"), _row("2026-09-19T02:50:00Z", main=12.47)]],
                     shadow=SHADOW)
    result = battery.car_status(gw, cfg, NOW)

    latest = result["最新の計測"]
    assert latest["計測時刻"] == "2026-09-19T11:55:00+09:00（約5分前）"
    assert latest["メインバッテリー電圧（車両の始動用バッテリー）"] == "12.45 V"
    assert latest["サブバッテリー電流"] == "-0.070 A（サブバッテリーから放電）"
    assert "満充電からの残量ではない" in latest["サブバッテリー積算電荷量"]
    assert result["推定状態"].startswith("エンジン停止中")
    assert len(result["直近の推移（新しい順）"]) == 2
    assert result["装置の設定"]["サブ→メイン充電リレー"] == "OFF"
    assert result["装置の設定"]["自動充電の開始電圧"].startswith("11.70 V")


def test_car_status_のSQLは対象装置と直近24時間に絞る(cfg):
    gw = FakeGateway()
    battery.car_status(gw, cfg, NOW)
    sql = gw.queries[0]
    assert "device_id = 'esp32-gw-aabbccddeeff'" in sql
    assert "type = 'battery'" in sql
    assert "ts >= '2026-09-18T03:00:00Z'" in sql
    assert "hour IN" in sql  # 24時間なのでhourまで絞る
    assert "ORDER BY ts DESC LIMIT 6" in sql


def test_car_status_はメイン電圧が高ければエンジン稼働中と推定する(cfg):
    gw = FakeGateway(query_results=[[_row("2026-09-19T02:58:00Z", main=14.2, current=3.2)]], shadow=SHADOW)
    result = battery.car_status(gw, cfg, NOW)
    assert result["推定状態"].startswith("エンジン稼働中と推定")
    assert result["最新の計測"]["サブバッテリー電流"] == "+3.200 A（サブバッテリーへ充電）"


def test_car_status_は充電リレーONのときエンジン稼働を推定しない(cfg):
    shadow = {"state": {"reported": {"charging": True}}}
    gw = FakeGateway(query_results=[[_row("2026-09-19T02:58:00Z", main=13.8)]], shadow=shadow)
    result = battery.car_status(gw, cfg, NOW)
    assert "推定できない" in result["推定状態"]
    assert result["装置の設定"]["サブ→メイン充電リレー"].startswith("ON")


def test_car_status_は古い計測を今の状態として扱わない(cfg):
    gw = FakeGateway(query_results=[[_row("2026-09-19T01:00:00Z")]], shadow=SHADOW)
    result = battery.car_status(gw, cfg, NOW)
    assert result["推定状態"].startswith("不明")
    assert "約2時間0分前" in result["最新の計測"]["計測時刻"]


def test_car_status_は受信が無いことを明示する(cfg):
    gw = FakeGateway(shadow=None)
    result = battery.car_status(gw, cfg, NOW)
    assert "受信した計測値が無い" in result["最新の計測"]
    assert result["推定状態"] == "不明"
    assert "見つからない" in result["装置の設定"]["状態"]


def _rollup(date_str, records):
    y, m, _ = date_str.split("-")
    return {f"rollup/year={y}/month={m}/{date_str}.json": "".join(json.dumps(r) + "\n" for r in records)}


def test_car_battery_history_は日ごとの電圧と充放電量を返す(cfg):
    objects = {
        **_rollup("2026-09-16", [
            {"date": "2026-09-16", "device_id": "other", "charge_ah": 9.0, "discharge_ah": 9.0},
            {"date": "2026-09-16", "device_id": "esp32-gw-aabbccddeeff", "charge_ah": 1.25, "discharge_ah": 0.5},
        ]),
        **_rollup("2026-09-17", []),  # 集計済みだがこの装置の行が無い
    }
    stats = [
        {"jst_date": "2026-09-16", "main_min": 12.3, "main_max": 14.4, "sub_min": 12.9, "sub_max": 13.6, "row_count": 288.0},
        {"jst_date": "2026-09-17", "main_min": 12.4, "main_max": 12.6, "sub_min": 13.0, "sub_max": 13.1, "row_count": 100.0},
        {"jst_date": "2026-09-19", "main_min": 12.4, "main_max": 12.5, "sub_min": 13.0, "sub_max": 13.1, "row_count": 144.0},
    ]
    gw = FakeGateway(objects=objects, query_results=[stats])
    result = battery.car_battery_history(gw, cfg, "2026-09-16", "2026-09-19", NOW)

    days = {d["日付"]: d for d in result["日ごとの記録"]}
    assert list(days) == ["2026-09-16", "2026-09-17", "2026-09-18", "2026-09-19"]
    assert days["2026-09-16"]["受信件数"] == 288
    assert days["2026-09-16"]["メインバッテリー電圧"] == "最低 12.30 V / 最高 14.40 V"
    assert days["2026-09-16"]["サブバッテリー充電量"] == "1.250 Ah"
    assert days["2026-09-16"]["サブバッテリー放電量"] == "0.500 Ah"
    assert days["2026-09-17"]["サブバッテリー充放電量"] == "集計済みだが充放電の記録が無い"
    assert "受信データが無い" in days["2026-09-18"]["状態"]
    assert days["2026-09-19"]["サブバッテリー充放電量"].startswith("未集計")


def test_car_battery_history_のSQLはJSTの暦日境界で絞る(cfg):
    gw = FakeGateway()
    battery.car_battery_history(gw, cfg, "2026-09-10", "2026-09-19", NOW)
    sql = gw.queries[0]
    assert "ts >= '2026-09-09T15:00:00Z'" in sql
    assert "ts < '2026-09-19T15:00:00Z'" in sql
    assert "hour" not in sql.split("WHERE", 1)[1].split("AND type")[0]  # 72時間超はhourで絞らない
    assert "date_add('hour', 9" in sql


def test_car_battery_history_は長すぎる期間を拒否する(cfg):
    with pytest.raises(ValueError, match="最大92日"):
        battery.car_battery_history(FakeGateway(), cfg, "2026-01-01", "2026-09-19", NOW)
