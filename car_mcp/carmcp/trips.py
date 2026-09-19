"""トリップ系の道具（car_trips・car_trip_detail）の中身。

データ源はtrip_analysis Lambdaが書くS3の結果ファイル:
  trip-analysis/{obd_device_id}/year=YYYY/month=MM/{session_start}_{session_end}_v{版}_{連番}.json
（year/monthはsession_endのUTC。詳細はHANDOFF_trip_analysis.md）

座標はファイルに入っていないので返しようがない。開始/終了地点の地名は、
CAR_EXPOSE_LOCATION=trueのときだけ返す（HANDOFF_mnemosyne.md 5章）。"""

import json
import re
from datetime import datetime, timedelta, timezone

from .aws import AwsGateway
from .config import Config
from .timeutil import JST, describe_duration, iso_jst, jst_day_start, validate_period

TRIP_PREFIX = "trip-analysis"
TRIPS_MAX_DAYS = 92
TRIPS_MAX_COUNT = 100

# 燃費の妥当性判定。1km未満の走行は燃料流量の積分誤差が支配的で燃費に意味が無く、
# 40km/Lを超える値は通信断で燃料消費が過小に積算されたとみなす（実データで372km/Lが出た）
ECONOMY_MIN_DISTANCE_KM = 1.0
ECONOMY_MAX_PLAUSIBLE_KM_L = 40.0

_TRIP_FILENAME_RE = re.compile(r"^(\d{10})_(\d{10})_v(\d+)_(\d+)\.json$")
_TRIP_ID_RE = re.compile(r"^(\d{10})_(\d{10})$")


def _device_prefix(cfg: Config) -> str:
    return f"{TRIP_PREFIX}/{cfg.obd_device_id}/"


def _month_prefix(cfg: Config, dt: datetime) -> str:
    return f"{_device_prefix(cfg)}year={dt.year:04d}/month={dt.month:02d}/"


def _months_between(start: datetime, end: datetime) -> list[datetime]:
    """start〜endを含む月（UTC）の初日を古い順に返す。"""
    months = []
    cur = datetime(start.year, start.month, 1, tzinfo=timezone.utc)
    while cur <= end:
        months.append(cur)
        cur = datetime(cur.year + cur.month // 12, cur.month % 12 + 1, 1, tzinfo=timezone.utc)
    return months


def _latest_versions(keys: list[str]) -> dict[tuple[int, int], str]:
    """同じ区間に複数の版・連番があれば最新のものだけを残し、(start, end) → キー の対応にする。"""
    best: dict[tuple[int, int], tuple[tuple[int, int], str]] = {}
    for key in keys:
        m = _TRIP_FILENAME_RE.match(key.rsplit("/", 1)[-1])
        if not m:
            continue
        span = (int(m.group(1)), int(m.group(2)))
        rank = (int(m.group(3)), int(m.group(4)))
        if span not in best or rank > best[span][0]:
            best[span] = (rank, key)
    return {span: key for span, (_, key) in best.items()}


def _analyzed_until(gw: AwsGateway, cfg: Config) -> int | None:
    """どこまでの走行がトリップ分析済みかを、最新の結果ファイルのsession_endから求める
    （trip_analysis/index.pyの_latest_session_endと同じ考え方）。"""
    years = gw.list_prefixes(_device_prefix(cfg))
    if not years:
        return None
    months = gw.list_prefixes(years[-1])
    if not months:
        return None
    spans = _latest_versions(gw.list_keys(months[-1]))
    return max((end for _, end in spans), default=None)


def _describe_economy(trip: dict) -> str:
    economy = trip.get("fuel_economy_km_l")
    distance = trip.get("distance_km") or 0.0
    if economy is None:
        return "算出できない（燃料消費が記録されていない）"
    if distance < ECONOMY_MIN_DISTANCE_KM:
        return f"{economy:.1f} km/L（走行距離が{ECONOMY_MIN_DISTANCE_KM:.0f}km未満のため参考にならない）"
    if economy > ECONOMY_MAX_PLAUSIBLE_KM_L:
        return (
            f"{economy:.1f} km/L（物理的にありえない値。OBDの通信断で燃料消費が少なく積算された"
            "可能性が高く、燃費として扱わない）"
        )
    return f"{economy:.1f} km/L"


def _trip_summary(cfg: Config, trip: dict) -> dict:
    start, end = int(trip["session_start"]), int(trip["session_end"])
    out = {
        "トリップID": f"{start:010d}_{end:010d}",
        "出発時刻": iso_jst(start),
        "到着時刻": iso_jst(end),
        "所要時間": describe_duration(trip.get("duration_sec", end - start)),
        "走行距離": f"{trip.get('distance_km', 0.0):.1f} km（GPSの移動距離の積算）",
        "燃料消費": None if trip.get("fuel_l") is None else f"{trip['fuel_l']:.2f} L（燃料流量の積分）",
        "燃費": _describe_economy(trip),
    }
    if cfg.expose_location:
        if trip.get("start_location"):
            out["出発地点"] = trip["start_location"]
        if trip.get("end_location"):
            out["到着地点"] = trip["end_location"]
    return {key: value for key, value in out.items() if value is not None}


def _coverage_notes(analyzed_until: int | None, range_end: datetime) -> list[str]:
    notes = [
        "トリップは、スマートフォンが車内にありOBD-IIのデータを受け取れた走行だけが記録される。"
        "記録が無いことは、走行しなかったことを意味しない",
    ]
    if analyzed_until is None:
        notes.append("トリップ分析がまだ一度も実行されていない")
    elif analyzed_until < range_end.timestamp():
        notes.append(
            f"トリップ分析は {iso_jst(analyzed_until)} までの走行しか済んでいない"
            "（分析は管理画面から手動で実行する）。それより後の走行はまだ一覧に出ない"
        )
    return notes


def car_trips(gw: AwsGateway, cfg: Config, start_date: str, end_date: str, now: datetime | None = None) -> dict:
    now = now or datetime.now(timezone.utc)
    today = now.astimezone(JST).date()
    start, end = validate_period(start_date, end_date, today, TRIPS_MAX_DAYS)
    range_start = jst_day_start(start)
    range_end = jst_day_start(end + timedelta(days=1))

    # フォルダはsession_end基準なので、期間の終わりをまたいで到着したトリップも拾えるよう1日余分に見る
    keys: list[str] = []
    for month in _months_between(range_start, range_end + timedelta(days=1)):
        keys.extend(gw.list_keys(_month_prefix(cfg, month)))
    spans = sorted(
        (span, key)
        for span, key in _latest_versions(keys).items()
        if range_start.timestamp() <= span[0] < range_end.timestamp()
    )

    truncated = len(spans) > TRIPS_MAX_COUNT
    trips = []
    for _, key in spans[:TRIPS_MAX_COUNT]:
        text = gw.get_text(key)
        if text is not None:
            trips.append(_trip_summary(cfg, json.loads(text)))

    notes = _coverage_notes(_analyzed_until(gw, cfg), min(range_end, now))
    if truncated:
        notes.append(f"期間内のトリップが{TRIPS_MAX_COUNT}件を超えたため、古い方から{TRIPS_MAX_COUNT}件だけを返した")

    return {
        "期間": f"{start.isoformat()} 〜 {end.isoformat()}（JSTの暦日、出発時刻で絞り込み）",
        "トリップ数": len(trips),
        "トリップ": trips,
        "注意": notes,
    }


def _signed_pct(value: float | None, meaning: str) -> str | None:
    return None if value is None else f"{value:+.1f} %（{meaning}）"


def car_trip_detail(gw: AwsGateway, cfg: Config, trip_id: str) -> dict:
    m = _TRIP_ID_RE.match(trip_id or "")
    if not m:
        raise ValueError("trip_id は car_trips が返したトリップID（例: 1726000000_1726003600）を指定する")
    start, end = int(m.group(1)), int(m.group(2))

    month = _month_prefix(cfg, datetime.fromtimestamp(end, tz=timezone.utc))
    key = _latest_versions(gw.list_keys(f"{month}{start:010d}_{end:010d}_v")).get((start, end))
    text = gw.get_text(key) if key else None
    if text is None:
        raise ValueError(f"トリップ {trip_id} が見つからない")
    trip = json.loads(text)

    detail = _trip_summary(cfg, trip)
    detail["OBDの記録件数"] = trip.get("row_count")
    detail["長期燃料補正（LTFT）の平均"] = _signed_pct(
        trip.get("ltft_avg"), "エンジンECUが学習した燃料噴射量の補正。正は増量、負は減量"
    )
    detail["短期燃料補正（STFT）の平均"] = _signed_pct(
        trip.get("stft_avg"), "その場の燃料噴射量の補正。正は増量、負は減量"
    )
    if trip.get("catalyst_temp_max") is not None:
        detail["触媒温度の最高値"] = f"{trip['catalyst_temp_max']:.0f} ℃"
    if trip.get("boost_kpa_max") is not None:
        detail["ブースト圧の最高値"] = f"{trip['boost_kpa_max']:.0f} kPa（吸気管圧力−大気圧。正の値は過給している）"
    if trip.get("coolant_start") is not None and trip.get("coolant_end") is not None:
        detail["冷却水温"] = f"出発時 {trip['coolant_start']:.0f} ℃ → 到着時 {trip['coolant_end']:.0f} ℃"
    return {key: value for key, value in detail.items() if value is not None}
