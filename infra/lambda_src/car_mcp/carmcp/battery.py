"""バッテリー系の道具（car_status・car_battery_history）の中身。

返り値は会話モデルが読むので、列名ではなく「何の値か・単位・どう読むか」を添えた
日本語のキーと値にする。コードで判定できることはここで判定して、事実として渡す
（trip_analysisでAIナレーティブが数値を誤判定した件の教訓。HANDOFF_mnemosyne.md 2章）。"""

import json
from datetime import date, datetime, timedelta, timezone

from .aws import AwsGateway, partition_filters
from .config import Config
from .thresholds import (
    ENGINE_RUNNING_MIN_V,
    HISTORY_MAX_DAYS,
    SEND_INTERVAL_SEC,
    STALE_AFTER_SEC,
    STATUS_LOOKBACK_HOURS,
    STATUS_RECENT_ROWS,
)
from .timeutil import (
    JST,
    date_range,
    describe_age,
    iso_jst,
    jst_day_start,
    parse_sensor_ts,
    sensor_ts,
    validate_period,
)

ROLLUP_PREFIX = "rollup/"


def _v(value: float | None, digits: int = 2) -> str | None:
    return None if value is None else f"{value:.{digits}f} V"


def _describe_current(amps: float | None) -> str | None:
    """サブバッテリー電流。INA228の読み値は符号反転済みで、積算電荷量（ah）と同じく
    充電で正・放電で負になる（battery_rollup/index.pyがahの増加を充電として扱うのと同じ向き）。"""
    if amps is None:
        return None
    if amps > 0:
        return f"+{amps:.3f} A（サブバッテリーへ充電）"
    if amps < 0:
        return f"{amps:.3f} A（サブバッテリーから放電）"
    return "0.000 A"


def _status_sql(cfg: Config, now: datetime) -> str:
    start = now - timedelta(hours=STATUS_LOOKBACK_HOURS)
    where = " AND ".join(
        partition_filters(start, now)
        + [
            "type = 'battery'",
            f"device_id = '{cfg.device_id}'",
            f"ts >= '{sensor_ts(start)}'",
        ]
    )
    return (
        "SELECT ts, main, sub, current, power, temp, ah FROM sensor_data "
        f"WHERE {where} ORDER BY ts DESC LIMIT {STATUS_RECENT_ROWS}"
    )


def _describe_shadow(shadow: dict | None) -> dict:
    if shadow is None:
        return {"状態": "装置の設定（Device Shadow）が見つからない"}
    reported = shadow.get("state", {}).get("reported", {})
    out: dict = {}
    if "charging" in reported:
        out["サブ→メイン充電リレー"] = (
            "ON（サブバッテリーからメインバッテリーへ充電中）" if reported["charging"] else "OFF"
        )
    if reported.get("chg_start_v") is not None:
        out["自動充電の開始電圧"] = f"{reported['chg_start_v']:.2f} V（メイン電圧がこれを下回るとサブから充電を始める）"
    if reported.get("chg_stop_v") is not None:
        out["自動充電の停止電圧"] = f"{reported['chg_stop_v']:.2f} V（メイン電圧がこれ以上になると充電をやめる）"
    if reported.get("fw_version"):
        out["ファームウェア"] = reported["fw_version"]
    if shadow.get("timestamp"):
        out["設定の最終報告"] = iso_jst(shadow["timestamp"])
    return out


def _infer_state(latest: dict, age_sec: float, charging: bool | None) -> str:
    if age_sec > STALE_AFTER_SEC:
        return (
            f"不明（最新の計測から通常の送信間隔{SEND_INTERVAL_SEC // 60}分を大きく超えて時間が経っている。"
            "装置の停止・圏外・電源断の可能性がある）"
        )
    if charging:
        return "サブ→メインの充電中のため、メイン電圧からエンジンの稼働は推定できない"
    main = latest.get("main")
    if main is None:
        return "不明（メイン電圧が記録されていない）"
    if main >= ENGINE_RUNNING_MIN_V:
        return f"エンジン稼働中と推定（メイン電圧が{ENGINE_RUNNING_MIN_V}V以上で、オルタネーターが発電している電圧帯）"
    return f"エンジン停止中（駐車中）と推定（メイン電圧が{ENGINE_RUNNING_MIN_V}V未満）"


def car_status(gw: AwsGateway, cfg: Config, now: datetime | None = None) -> dict:
    now = now or datetime.now(timezone.utc)
    rows = gw.query(_status_sql(cfg, now))
    shadow = gw.get_shadow(cfg.device_id)
    settings = _describe_shadow(shadow)
    charging = shadow.get("state", {}).get("reported", {}).get("charging") if shadow else None

    result: dict = {"対象装置": cfg.device_id, "現在時刻": iso_jst(now)}
    if not rows:
        result["最新の計測"] = (
            f"過去{STATUS_LOOKBACK_HOURS}時間に受信した計測値が無い（装置の停止・圏外・電源断の可能性がある）"
        )
        result["推定状態"] = "不明"
        result["装置の設定"] = settings
        return result

    latest = rows[0]
    measured_at = parse_sensor_ts(latest["ts"])
    age_sec = (now - measured_at).total_seconds()
    result["最新の計測"] = {
        "計測時刻": f"{iso_jst(measured_at)}（{describe_age(age_sec)}）",
        "メインバッテリー電圧（車両の始動用バッテリー）": _v(latest.get("main")),
        "サブバッテリー電圧": _v(latest.get("sub")),
        "サブバッテリー電流": _describe_current(latest.get("current")),
        "サブバッテリー電力": None if latest.get("power") is None else f"{latest['power']:.2f} W",
        "サブバッテリー積算電荷量": None
        if latest.get("ah") is None
        else f"{latest['ah']:.3f} Ah（充電で増え放電で減る相対値。満充電からの残量ではない）",
        "計測基板の温度": None
        if latest.get("temp") is None
        else f"{latest['temp']:.1f} ℃（電流センサー内蔵の温度計。外気温ではない）",
    }
    result["推定状態"] = _infer_state(latest, age_sec, charging)
    result["直近の推移（新しい順）"] = [
        {
            "計測時刻": iso_jst(parse_sensor_ts(r["ts"])),
            "メイン電圧": _v(r.get("main")),
            "サブ電圧": _v(r.get("sub")),
            "サブ電流": None if r.get("current") is None else f"{r['current']:+.3f} A",
        }
        for r in rows
    ]
    result["装置の設定"] = settings
    return result


def _history_sql(cfg: Config, start: date, end: date) -> str:
    range_start = jst_day_start(start)
    range_end = jst_day_start(end + timedelta(days=1))  # 排他境界
    where = " AND ".join(
        partition_filters(range_start, range_end - timedelta(seconds=1))
        + [
            "type = 'battery'",
            f"device_id = '{cfg.device_id}'",
            f"ts >= '{sensor_ts(range_start)}'",
            f"ts < '{sensor_ts(range_end)}'",
        ]
    )
    return f"""
SELECT
  date_format(date_add('hour', 9, from_iso8601_timestamp(ts)), '%Y-%m-%d') AS jst_date,
  MIN(main) AS main_min, MAX(main) AS main_max,
  MIN(sub) AS sub_min, MAX(sub) AS sub_max,
  COUNT(*) AS row_count
FROM sensor_data
WHERE {where}
GROUP BY 1
ORDER BY 1
"""


def _rollup_key(d: date) -> str:
    """battery_rollup/index.pyの_rollup_keyと同じ規則。"""
    return f"{ROLLUP_PREFIX}year={d.year:04d}/month={d.month:02d}/{d.isoformat()}.json"


def _load_rollup(gw: AwsGateway, cfg: Config, d: date) -> dict | None:
    """その日のrollupレコードを返す。ファイル自体が無ければNone（未集計）、
    ファイルはあるがこの装置の行が無ければ空dict（集計済みでデータ無し）。"""
    text = gw.get_text(_rollup_key(d))
    if text is None:
        return None
    for line in text.splitlines():
        if not line.strip():
            continue
        record = json.loads(line)
        if record.get("device_id") == cfg.device_id:
            return record
    return {}


def _range_text(lo: float | None, hi: float | None) -> str | None:
    if lo is None or hi is None:
        return None
    return f"最低 {lo:.2f} V / 最高 {hi:.2f} V"


def car_battery_history(
    gw: AwsGateway, cfg: Config, start_date: str, end_date: str, now: datetime | None = None
) -> dict:
    now = now or datetime.now(timezone.utc)
    today = now.astimezone(JST).date()
    start, end = validate_period(start_date, end_date, today, HISTORY_MAX_DAYS)

    stats = {r["jst_date"]: r for r in gw.query(_history_sql(cfg, start, end))}

    days = []
    for d in date_range(start, end):
        key = d.isoformat()
        stat = stats.get(key)
        day: dict = {"日付": key}
        if stat is None:
            day["状態"] = "この日の受信データが無い（装置の停止・圏外・電源断の可能性がある）"
            days.append(day)
            continue

        day["受信件数"] = int(stat["row_count"])
        day["メインバッテリー電圧"] = _range_text(stat.get("main_min"), stat.get("main_max"))
        day["サブバッテリー電圧"] = _range_text(stat.get("sub_min"), stat.get("sub_max"))

        rollup = _load_rollup(gw, cfg, d)
        if rollup is None:
            day["サブバッテリー充放電量"] = "未集計（日次集計は毎日1時ごろに前日分を作る。当日分はまだ無い）"
        elif not rollup:
            day["サブバッテリー充放電量"] = "集計済みだが充放電の記録が無い"
        else:
            day["サブバッテリー充電量"] = f"{rollup.get('charge_ah') or 0.0:.3f} Ah"
            day["サブバッテリー放電量"] = f"{rollup.get('discharge_ah') or 0.0:.3f} Ah"
        days.append(day)

    return {
        "対象装置": cfg.device_id,
        "期間": f"{start.isoformat()} 〜 {end.isoformat()}（JSTの暦日）",
        "日ごとの記録": days,
        "読み方": [
            "充電量・放電量は、サブバッテリーの積算電荷量の増減を日ごとに足し合わせた値",
            f"受信件数は通常{SEND_INTERVAL_SEC // 60}分に1件（1日で約{86400 // SEND_INTERVAL_SEC}件）。"
            "少ない日は装置が止まっていた時間がある",
        ],
    }
