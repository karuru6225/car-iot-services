"""
バッテリー充放電量の日次rollupバッチ（EventBridge Scheduler起動、API Gateway非経由）

目的: Web管理画面（web/index.html）とは別に、別リポジトリclaude-opentelemetryの
Grafanaダッシュボードが「当日・前日を除く長期間」の日次充電量・放電量を常時表示
ダッシュボードで見られるようにする。Grafanaの自動リフレッシュのたびにAthenaへ
オンデマンドで生データを長期間クエリするとコストが期間の伸びに比例して増えるため、
このLambdaが毎日1回、日次集計を先に計算してS3のrollup/prefixへ書き出しておき、
Grafanaは既存のAthenaデータソース経由で集計済みの小さいデータだけを読む
（Grafana Infinityデータソース経由も検討したが「1クエリ=1URL取得」で複数日を
束ねて取得できず長期間グラフに向かないため不採用）。

S3キー構造: rollup/year=YYYY/month=MM/YYYY-MM-DD.json（NDJSON、1行1レコード）。
raw/と同じ発想でyear/monthのHiveパーティション分割をしておき、Grafana側のAthena
クエリが表示期間に応じてパーティションを絞り込めるようにする
（infra/battery_rollup.tfのGlueテーブル定義、パーティションプロジェクション使用）。

充放電量の算出方法:
  `ah`（esp32_iot_gateway/src/device/ina228.cpp の readCharge()）はINA228の40bit
  符号付きCHARGEレジスタ由来の「正味の積算電荷量」で、放電で減少・充電で増加する
  値であり単調増加のカウンタではない。そのため日ごとの充電量・放電量を分けて出す
  には、隣接する2行のah差分（delta = 当該行のah - 直前行のah）を計算し、
  delta>0を充電量、delta<0の絶対値を放電量として日単位で合算する。

watermark方式（DynamoDB等の可変ステートを持たない）:
  「どこまでrollup済みか」はS3のrollup/配下で一番新しい日付のファイルから逆算する
  （trip_analysis/index.pyと同じ設計思想）。このため未処理日は必ず古い順・隙間なく
  処理する必要がある。新しい日付を先に処理すると、その時点でwatermarkが「最新まで
  処理済み」と誤認し、間に挟まれた古い未処理日が永久にスキップされてしまう。

初回バックフィルのタイムアウト対策:
  初回導入時は`_earliest_battery_date()`まで一気に遡るため対象日数が数十〜数百日
  規模になりうる。MAX_DAYS_PER_RUN（デフォルト30）で1回の実行の影響範囲（Athena
  結果セットサイズ・S3書き込み件数）を抑え、複数回の実行（毎日の定期実行、または
  手動でsince/untilを指定した連続invoke）に分けて自然に追いつかせる。恒常的な
  ホットパスに一括処理特有の複雑さを持ち込まない、というcompact Lambdaのバックフィル
  簡素化の教訓を踏襲する。

LAGの日境界問題（シード日）:
  対象日だけに絞ってAthenaのLAG(ah)を計算すると、各日の最初の1行のLAGが必ずNULLに
  なり、日境界をまたぐ差分（前日最終行との差）が欠落する。そのため対象範囲の前に
  1日分の「シード日」を追加でクエリに含め、LAG計算にのみ使い出力からは除外する。

以下のヘルパーはtrip_analysis/index.py・query/index.pyとロジックが重複するが、
このリポジトリにLambda Layer等の共有機構が無いため今回も共通化せず重複を許容する
（Rule of Three: 4つ目の類似Lambdaが必要になった時点でLayer化を検討する）:
  _partition_filters / _run_athena_query / _parse_athena_results / _list_partition_values
    ... query/index.py・trip_analysis/index.pyと同種

event引数（手動バックフィル用。省略時は環境変数のデフォルト値を使う）:
  {"since": "2026-05-01", "until": "2026-05-31"}
"""

import json
import os
import re
import time
from datetime import datetime, timedelta, timezone

import boto3

athena = boto3.client("athena")
s3 = boto3.client("s3")

S3_BUCKET = os.environ["S3_BUCKET"]
ATHENA_DATABASE = os.environ["ATHENA_DATABASE"]
ATHENA_WORKGROUP = os.environ["ATHENA_WORKGROUP"]

REPROCESS_LOOKBACK_DAYS = int(os.environ.get("REPROCESS_LOOKBACK_DAYS", "3"))
MAX_DAYS_PER_RUN = int(os.environ.get("MAX_DAYS_PER_RUN", "30"))

ATHENA_POLL_INTERVAL_SEC = 2.0
ATHENA_POLL_TIMEOUT_SEC = int(os.environ.get("ATHENA_POLL_TIMEOUT_SEC", "600"))

ROLLUP_PREFIX = "rollup/"
JST = timezone(timedelta(hours=9))


def _jst_date_str(dt: datetime) -> str:
    """tz-awareなdatetimeをJST暦日文字列(YYYY-MM-DD)に変換する。"""
    return dt.astimezone(JST).strftime("%Y-%m-%d")


def _jst_day_start_utc(date_str: str) -> datetime:
    """JST暦日 date_str の 00:00 をUTCのdatetimeで返す。"""
    y, m, d = (int(x) for x in date_str.split("-"))
    return datetime(y, m, d, tzinfo=JST).astimezone(timezone.utc)


def _date_range(start: str, end: str) -> list[str]:
    """start〜end（JST暦日文字列、両端含む）を古い順に列挙する。startがendより後なら空。"""
    start_dt = datetime.strptime(start, "%Y-%m-%d")
    end_dt = datetime.strptime(end, "%Y-%m-%d")
    dates = []
    cur = start_dt
    while cur <= end_dt:
        dates.append(cur.strftime("%Y-%m-%d"))
        cur += timedelta(days=1)
    return dates


def _partition_filters(start_dt: datetime, end_dt: datetime) -> list[str]:
    """start_dt〜end_dt（tz-aware UTC datetime、endは排他境界の1つ手前まで含めてよい）を
    カバーする最小限のパーティションフィルタを返す。query/index.pyの
    _partition_filters_rangeと同一ロジック（モジュール冒頭docstring記載の方針により
    共通化しない）。"""
    cur = start_dt.replace(minute=0, second=0, microsecond=0)

    years, months, days, hours = set(), set(), set(), set()
    while cur <= end_dt:
        years.add(cur.strftime("%Y"))
        months.add(cur.strftime("%m"))
        days.add(cur.strftime("%d"))
        hours.add(cur.strftime("%H"))
        cur += timedelta(hours=1)

    def _in(col, vals):
        v = sorted(vals)
        return f"{col} = '{v[0]}'" if len(v) == 1 else f"{col} IN ({', '.join(repr(x) for x in v)})"

    return [_in("year", years), _in("month", months), _in("day", days), _in("hour", hours)]


def _list_partition_values(prefix: str, key_name: str) -> list[str]:
    """S3 Hive パーティション（key_name=value/）のバリュー一覧を返す。
    trip_analysis/index.pyの_list_partition_valuesと同一ロジック。"""
    values = []
    kwargs = {"Bucket": S3_BUCKET, "Prefix": prefix, "Delimiter": "/"}
    while True:
        resp = s3.list_objects_v2(**kwargs)
        for cp in resp.get("CommonPrefixes", []):
            part = cp["Prefix"].rstrip("/").split("/")[-1]
            if "=" in part and part.split("=")[0] == key_name:
                values.append(part.split("=")[1])
        if not resp.get("IsTruncated"):
            break
        kwargs["ContinuationToken"] = resp["NextContinuationToken"]
    return sorted(values)


def _earliest_battery_date() -> str | None:
    """raw/の最古のhourパーティションをJST暦日に変換して返す（初回バックフィルの開始点）。
    trip_analysis/index.pyの_earliest_obd_tsと同種のドリルダウン手法（年→月→日→時）。"""
    years = _list_partition_values("raw/", "year")
    if not years:
        return None
    year = years[0]
    months = _list_partition_values(f"raw/year={year}/", "month")
    if not months:
        return None
    month = months[0]
    days = _list_partition_values(f"raw/year={year}/month={month}/", "day")
    if not days:
        return None
    day = days[0]
    hours = _list_partition_values(f"raw/year={year}/month={month}/day={day}/", "hour")
    hour = hours[0] if hours else "00"
    dt = datetime(int(year), int(month), int(day), int(hour), tzinfo=timezone.utc)
    return _jst_date_str(dt)


_ROLLUP_FILENAME_RE = re.compile(r"^(\d{4}-\d{2}-\d{2})\.json$")


def _latest_rollup_date() -> str | None:
    """rollup/配下のファイル名(YYYY-MM-DD.json、year=/month=パーティション配下にある)から
    最新の日付を返す。ファイル名は常にフルパスの末尾コンポーネントとして取り出せるため、
    パーティションの有無に関わらずtrip_analysisのような年月ドリルダウンは不要、全件走査で
    足りる（1年でも365件程度）。"""
    dates = []
    paginator = s3.get_paginator("list_objects_v2")
    for page in paginator.paginate(Bucket=S3_BUCKET, Prefix=ROLLUP_PREFIX):
        for obj in page.get("Contents", []):
            filename = obj["Key"].rsplit("/", 1)[-1]
            m = _ROLLUP_FILENAME_RE.match(filename)
            if m:
                dates.append(m.group(1))
    return max(dates) if dates else None


def _target_dates(now_utc: datetime, event: dict) -> list[str]:
    """処理対象日（JST暦日、古い順）のリストを返す。必ず連続した範囲を返し、
    一部の日だけを先に埋めるような飛び地処理はしない（watermark方式の要件、
    モジュール冒頭docstring参照）。"""
    since = event.get("since")
    until = event.get("until")
    if since and until:
        all_dates = _date_range(since, until)
    else:
        end = _jst_date_str(now_utc - timedelta(days=1))  # 今日(JST)の前日まで
        latest = _latest_rollup_date()
        if latest is not None:
            start_dt = datetime.strptime(latest, "%Y-%m-%d") - timedelta(days=REPROCESS_LOOKBACK_DAYS)
            start = start_dt.strftime("%Y-%m-%d")
        else:
            start = _earliest_battery_date()  # 初回フルバックフィル
        if start is None or start > end:
            return []
        all_dates = _date_range(start, end)

    return all_dates[:MAX_DAYS_PER_RUN]


def _build_query(target_dates: list[str]) -> str:
    """対象範囲全体（シード日を含む）を1本のAthenaクエリでカバーするSQLを組み立てる。
    日ごとに個別クエリを発行しない（複数日バックフィルではクエリ発行回数×ポーリング
    待ちのオーバーヘッドを大きく減らせる）。シード日（対象範囲の前日）はLAG計算にのみ
    使い、GROUP BYの出力には含めない。"""
    first_date, last_date = target_dates[0], target_dates[-1]
    seed_date = (datetime.strptime(first_date, "%Y-%m-%d") - timedelta(days=1)).strftime("%Y-%m-%d")
    day_after_last = (datetime.strptime(last_date, "%Y-%m-%d") + timedelta(days=1)).strftime("%Y-%m-%d")

    range_start_dt = _jst_day_start_utc(seed_date)
    range_end_dt = _jst_day_start_utc(day_after_last)  # 排他境界（この時刻ちょうどは含めない）

    partition_filters = _partition_filters(range_start_dt, range_end_dt - timedelta(seconds=1))
    start_iso = range_start_dt.strftime("%Y-%m-%dT%H:%M:%SZ")
    end_iso = range_end_dt.strftime("%Y-%m-%dT%H:%M:%SZ")

    where = " AND ".join(
        partition_filters
        + [
            "type = 'battery'",
            "ah IS NOT NULL",
            f"ts >= '{start_iso}'",
            f"ts < '{end_iso}'",
        ]
    )

    return f"""
WITH ordered AS (
  SELECT
    device_id,
    ts,
    ah - LAG(ah) OVER (PARTITION BY device_id ORDER BY ts) AS delta_ah
  FROM sensor_data
  WHERE {where}
)
SELECT
  date_format(date_add('hour', 9, from_iso8601_timestamp(ts)), '%Y-%m-%d') AS jst_date,
  device_id,
  SUM(CASE WHEN delta_ah > 0 THEN delta_ah ELSE 0 END)  AS charge_ah,
  SUM(CASE WHEN delta_ah < 0 THEN -delta_ah ELSE 0 END) AS discharge_ah,
  COUNT(*) AS row_count
FROM ordered
WHERE delta_ah IS NOT NULL
GROUP BY 1, 2
ORDER BY 1, 2
"""


def _run_athena_query(query: str) -> list[dict]:
    """クエリを投げてSUCCEEDEDになるまで同一Lambda呼び出し内でポーリングし、結果を返す。
    trip_analysis/index.pyから移植。"""
    resp = athena.start_query_execution(
        QueryString=query,
        QueryExecutionContext={"Database": ATHENA_DATABASE},
        WorkGroup=ATHENA_WORKGROUP,
    )
    execution_id = resp["QueryExecutionId"]

    deadline = time.time() + ATHENA_POLL_TIMEOUT_SEC
    while True:
        status = athena.get_query_execution(QueryExecutionId=execution_id)
        state = status["QueryExecution"]["Status"]["State"]
        if state == "SUCCEEDED":
            break
        if state in ("FAILED", "CANCELLED"):
            reason = status["QueryExecution"]["Status"].get("StateChangeReason", "")
            raise RuntimeError(f"Athena query {state}: {reason}")
        if time.time() > deadline:
            raise TimeoutError(f"Athena query timed out (execution_id={execution_id})")
        time.sleep(ATHENA_POLL_INTERVAL_SEC)

    return _parse_athena_results(execution_id)


def _parse_athena_results(execution_id: str) -> list[dict]:
    """クエリ結果をdictのリストに変換する。数値はfloatに変換する。
    trip_analysis/index.pyから移植（モジュール冒頭docstring記載の方針により共通化しない）。"""
    rows = []
    col_names = None
    kwargs = {"QueryExecutionId": execution_id}

    while True:
        result = athena.get_query_results(**kwargs)
        result_rows = result["ResultSet"]["Rows"]

        if col_names is None:
            col_names = [c["Label"] for c in result["ResultSet"]["ResultSetMetadata"]["ColumnInfo"]]
            result_rows = result_rows[1:]  # ヘッダー行スキップ

        for row in result_rows:
            values = [d.get("VarCharValue") for d in row["Data"]]
            obj = {}
            for col, val in zip(col_names, values):
                if val is None:
                    obj[col] = None
                    continue
                try:
                    obj[col] = float(val)
                except (ValueError, TypeError):
                    obj[col] = val
            rows.append(obj)

        next_token = result.get("NextToken")
        if not next_token:
            break
        kwargs["NextToken"] = next_token

    return rows


def _group_by_date(rows: list[dict], target_dates: list[str]) -> dict[str, list[dict]]:
    """Athena結果を日付ごとにグルーピングする。シード日の行（target_datesに含まれない
    日）は自動的に除外される。対象日でも結果が無ければ空リストのままになる。"""
    target_set = set(target_dates)
    grouped: dict[str, list[dict]] = {d: [] for d in target_dates}
    for row in rows:
        date_str = row["jst_date"]
        if date_str not in target_set:
            continue
        grouped[date_str].append(
            {
                "date": date_str,
                "device_id": row["device_id"],
                "charge_ah": row["charge_ah"] or 0.0,
                "discharge_ah": row["discharge_ah"] or 0.0,
                "row_count": int(row["row_count"]),
            }
        )
    return grouped


def _rollup_key(date_str: str) -> str:
    """rollup/year=YYYY/month=MM/YYYY-MM-DD.json 形式のS3キーを返す。年月でHive
    パーティション分割することで、Grafana側のAthenaクエリが表示期間に応じて
    year/monthパーティションを絞り込める（infra/battery_rollup.tfのGlueテーブル定義参照）。"""
    year, month, _day = date_str.split("-")
    return f"{ROLLUP_PREFIX}year={year}/month={month}/{date_str}.json"


def _write_rollup_file(date_str: str, records: list[dict]) -> None:
    """rollup/year=/month=/{date_str}.jsonへ書き込む。常に全体を上書きする（追記ではない）。
    結果0件でも空ファイルを書く。書かないと_latest_rollup_date()ベースのwatermarkが
    前進せず、データ欠損日を毎回再クエリし続けてしまうため。

    NDJSON（改行区切り、1行1レコード）で書く。sensor_dataテーブルと同じ
    org.openx.data.jsonserde.JsonSerDeでAthenaから読むため、JSON配列一括では
    パースできない（1オブジェクト=1行が前提）。"""
    key = _rollup_key(date_str)
    body = "".join(json.dumps(r, ensure_ascii=False) + "\n" for r in records)
    s3.put_object(
        Bucket=S3_BUCKET,
        Key=key,
        Body=body.encode("utf-8"),
        ContentType="application/json",
    )


def handler(event, context):
    event = event or {}
    now_utc = datetime.now(timezone.utc)

    target_dates = _target_dates(now_utc, event)
    if not target_dates:
        print("[OK] battery_rollup: no target dates, nothing to do")
        return {"target_dates": [], "written": 0}

    query = _build_query(target_dates)
    rows = _run_athena_query(query)
    grouped = _group_by_date(rows, target_dates)

    for date_str in target_dates:
        _write_rollup_file(date_str, grouped[date_str])

    print(
        f"[OK] battery_rollup: wrote {len(target_dates)} day(s) "
        f"{target_dates[0]}..{target_dates[-1]}"
    )
    return {"target_dates": target_dates, "written": len(target_dates)}
