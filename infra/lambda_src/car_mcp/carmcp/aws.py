"""AWSへの読み取りアクセスをまとめた層。道具（battery.py・trips.py）はこのクラスの
メソッドだけを使い、テストでは同じメソッドを持つ偽物に差し替える。

Athenaの実行〜ポーリング〜結果のパースはbattery_rollup/index.py等の
_run_athena_query・_parse_athena_resultsと同じ手順。このリポジトリにLambda Layer等の
共有機構が無いため、他のLambdaと同じく重複を許容する。"""

import json
import time
from datetime import datetime, timedelta

import boto3
from botocore.exceptions import ClientError

from .config import Config
from .thresholds import ATHENA_POLL_TIMEOUT_SEC

ATHENA_POLL_INTERVAL_SEC = 1.0


class AthenaError(RuntimeError):
    pass


def partition_filters(start_dt: datetime, end_dt: datetime) -> list[str]:
    """start_dt〜end_dt（UTC、両端含む）をカバーするraw/・obd/のパーティション条件を返す。
    パーティションはUTCの年/月/日/時。72時間を超える範囲ではhourの条件を付けない
    （24通りすべてが含まれて意味が無くなるため。query/index.pyと同じ扱い）。"""
    cur = start_dt.replace(minute=0, second=0, microsecond=0)
    years, months, days, hours = set(), set(), set(), set()
    while cur <= end_dt:
        years.add(cur.strftime("%Y"))
        months.add(cur.strftime("%m"))
        days.add(cur.strftime("%d"))
        hours.add(cur.strftime("%H"))
        cur += timedelta(hours=1)

    def _in(col: str, vals: set[str]) -> str:
        v = sorted(vals)
        return f"{col} = '{v[0]}'" if len(v) == 1 else f"{col} IN ({', '.join(repr(x) for x in v)})"

    filters = [_in("year", years), _in("month", months), _in("day", days)]
    if (end_dt - start_dt) <= timedelta(hours=72):
        filters.append(_in("hour", hours))
    return filters


class AwsGateway:
    def __init__(self, cfg: Config, session: boto3.Session | None = None):
        session = session or boto3.Session()
        self._cfg = cfg
        self._athena = session.client("athena")
        self._s3 = session.client("s3")
        self._iot_data = session.client("iot-data", endpoint_url=cfg.iot_endpoint)

    def query(self, sql: str) -> list[dict]:
        """Athenaでクエリを実行し、結果を行ごとのdictで返す。数値に見える値はfloatにする。"""
        resp = self._athena.start_query_execution(
            QueryString=sql,
            QueryExecutionContext={"Database": self._cfg.athena_database},
            WorkGroup=self._cfg.athena_workgroup,
        )
        execution_id = resp["QueryExecutionId"]

        deadline = time.monotonic() + ATHENA_POLL_TIMEOUT_SEC
        while True:
            status = self._athena.get_query_execution(QueryExecutionId=execution_id)["QueryExecution"]["Status"]
            state = status["State"]
            if state == "SUCCEEDED":
                break
            if state in ("FAILED", "CANCELLED"):
                raise AthenaError(f"Athenaのクエリが{state}: {status.get('StateChangeReason', '')}")
            if time.monotonic() > deadline:
                self._athena.stop_query_execution(QueryExecutionId=execution_id)
                raise AthenaError(f"Athenaのクエリが{ATHENA_POLL_TIMEOUT_SEC}秒で終わらなかった")
            time.sleep(ATHENA_POLL_INTERVAL_SEC)

        rows: list[dict] = []
        col_names = None
        kwargs = {"QueryExecutionId": execution_id}
        while True:
            result = self._athena.get_query_results(**kwargs)
            result_rows = result["ResultSet"]["Rows"]
            if col_names is None:
                col_names = [c["Label"] for c in result["ResultSet"]["ResultSetMetadata"]["ColumnInfo"]]
                result_rows = result_rows[1:]  # ヘッダー行
            for row in result_rows:
                obj = {}
                for col, cell in zip(col_names, row["Data"]):
                    val = cell.get("VarCharValue")
                    try:
                        obj[col] = float(val) if val is not None else None
                    except ValueError:
                        obj[col] = val
                rows.append(obj)
            if not result.get("NextToken"):
                return rows
            kwargs["NextToken"] = result["NextToken"]

    def list_prefixes(self, prefix: str) -> list[str]:
        """prefix直下のフォルダ（CommonPrefixes）を辞書順で返す。"""
        found = []
        paginator = self._s3.get_paginator("list_objects_v2")
        for page in paginator.paginate(Bucket=self._cfg.s3_bucket, Prefix=prefix, Delimiter="/"):
            found.extend(cp["Prefix"] for cp in page.get("CommonPrefixes", []))
        return sorted(found)

    def list_keys(self, prefix: str) -> list[str]:
        """prefix配下のオブジェクトキーを辞書順で返す。"""
        found = []
        paginator = self._s3.get_paginator("list_objects_v2")
        for page in paginator.paginate(Bucket=self._cfg.s3_bucket, Prefix=prefix):
            found.extend(o["Key"] for o in page.get("Contents", []))
        return sorted(found)

    def get_text(self, key: str) -> str | None:
        """オブジェクトの中身を文字列で返す。存在しなければNone。"""
        try:
            obj = self._s3.get_object(Bucket=self._cfg.s3_bucket, Key=key)
        except ClientError as e:
            if e.response.get("Error", {}).get("Code") in ("NoSuchKey", "404"):
                return None
            raise
        return obj["Body"].read().decode("utf-8")

    def get_shadow(self, thing_name: str) -> dict | None:
        """Device Shadowのドキュメント全体を返す。Shadowが無ければNone。"""
        try:
            resp = self._iot_data.get_thing_shadow(thingName=thing_name)
        except self._iot_data.exceptions.ResourceNotFoundException:
            return None
        return json.loads(resp["payload"].read())
