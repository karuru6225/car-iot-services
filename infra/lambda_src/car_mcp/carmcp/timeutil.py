"""時刻・日付の変換。会話モデルへ渡す時刻はすべて時差付きのJST ISO 8601にする
（UTCのエポック秒だと会話モデルが日付を取り違えるため。HANDOFF_mnemosyne.md 2章）。"""

import re
from datetime import date, datetime, timedelta, timezone

JST = timezone(timedelta(hours=9))

_DATE_RE = re.compile(r"^\d{4}-\d{2}-\d{2}$")


def iso_jst(value: datetime | int | float) -> str:
    """tz-awareなdatetimeかエポック秒を、JSTのISO 8601文字列（秒まで）にする。"""
    if isinstance(value, (int, float)):
        value = datetime.fromtimestamp(value, tz=timezone.utc)
    return value.astimezone(JST).isoformat(timespec="seconds")


def parse_sensor_ts(ts: str) -> datetime:
    """sensor_dataのts（ingest Lambdaが書く "YYYY-MM-DDTHH:MM:SSZ"）をUTCのdatetimeにする。"""
    return datetime.strptime(ts, "%Y-%m-%dT%H:%M:%SZ").replace(tzinfo=timezone.utc)


def sensor_ts(dt: datetime) -> str:
    """UTCのdatetimeをsensor_dataのtsと同じ書式にする（文字列比較でWHERE句に使う）。"""
    return dt.astimezone(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def parse_date(value: str, field: str) -> date:
    """会話モデルから渡された日付（YYYY-MM-DD、JSTの暦日）を検証して返す。"""
    if not isinstance(value, str) or not _DATE_RE.match(value):
        raise ValueError(f"{field} は YYYY-MM-DD 形式で指定する（受け取った値: {value!r}）")
    try:
        return date.fromisoformat(value)
    except ValueError:
        raise ValueError(f"{field} が存在しない日付になっている（受け取った値: {value!r}）") from None


def validate_period(start_date: str, end_date: str, today: date, max_days: int) -> tuple[date, date]:
    """会話モデルから渡された期間（JSTの暦日）を検証する。未来の終了日は今日に丸める。"""
    start = parse_date(start_date, "start_date")
    end = parse_date(end_date, "end_date")
    if end < start:
        raise ValueError("end_date が start_date より前になっている")
    if start > today:
        raise ValueError(f"start_date が未来の日付になっている（今日は {today.isoformat()}）")
    end = min(end, today)
    if (end - start).days + 1 > max_days:
        raise ValueError(f"期間は最大{max_days}日まで（Athenaのスキャン量と応答時間を抑えるため）")
    return start, end


def jst_day_start(d: date) -> datetime:
    """JST暦日の0時をUTCのdatetimeで返す。"""
    return datetime(d.year, d.month, d.day, tzinfo=JST).astimezone(timezone.utc)


def date_range(start: date, end: date) -> list[date]:
    """start〜end（両端含む）を古い順に列挙する。"""
    return [start + timedelta(days=i) for i in range((end - start).days + 1)]


def describe_age(seconds: float) -> str:
    """経過時間を「約12分前」のような読み違えにくい日本語にする。"""
    seconds = max(0, int(seconds))
    if seconds < 60:
        return f"{seconds}秒前"
    minutes = seconds // 60
    if minutes < 60:
        return f"約{minutes}分前"
    hours, minutes = divmod(minutes, 60)
    if hours < 48:
        return f"約{hours}時間{minutes}分前"
    return f"約{hours // 24}日前"


def describe_duration(seconds: float) -> str:
    """所要時間を「1時間2分」のような日本語にする。"""
    seconds = max(0, int(seconds))
    hours, rem = divmod(seconds, 3600)
    minutes = rem // 60
    if hours:
        return f"{hours}時間{minutes}分"
    if minutes:
        return f"{minutes}分"
    return f"{seconds}秒"
