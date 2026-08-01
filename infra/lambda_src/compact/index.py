"""
S3 raw データの定期 compaction

ingest Lambda（lambda_src/ingest/index.py）はメッセージ1件ごとに1オブジェクトを
raw/year=YYYY/month=MM/day=DD/hour=HH/{device_id}-{uuid8}.json に書き込む。Grafanaが
Athena経由でこのテーブルを高頻度（約15分おき）でクエリしており、1hourパーティション
あたり平均約29ファイルを毎回開くことがS3リクエスト課金（Tier1/Tier2）の主因になって
いる（esp32_iot_gateway/CONTEXT.md 参照）。

EventBridge Scheduler により毎時実行される（UTC評価）。確定済み（現在進行中・直前1
hourを安全マージンとして除外）hourパーティションを対象に、複数の小さいJSONファイルを
1つの改行区切りJSON（NDJSON）ファイル `_merged.json` にまとめる。

冪等性: `_merged.json` 以外のファイル（straggler）が無ければ何もしない。`_merged.json`
が既にあればstragglerを吸収して上書き、無ければstragglerが2件以上のときだけ新規作成
する（1件だけの場合は圧縮する意味がないためスキップ）。これによりオフラインバッファ
経由で後から古いhourパーティションに書き込まれるケース（ingest Lambdaはpayloadの
`ts`でパーティションを決めるため起こりうる）にも次回実行時に自然に対応できる。

パーティション値（year/month/day/hour）はS3パスから機械的に決まる（Glueのpartition
projection方式）ため、hour境界をまたぐマージは行わない。

マージ元の小ファイルは即削除せず、ARCHIVE_BUCKETへcopy_objectで退避してから
delete_objectする（corruptedバケットと同じ「安全側に倒す」設計。90日でS3 Lifecycle
により自動削除）。copy/deleteが失敗した場合はそのstragglerを次回に持ち越す（＝次回
また`_merged.json`に追記されうる＝行の重複が発生しうるが、データ消失よりマシという
判断で許容する）。

並列化: パーティション内のGet/Copy/Delete処理はThreadPoolExecutorで並列化する
（既存Lambda群に前例のない設計だが、バックフィル時に非現実的な実行時間になるのを
避けるため。boto3クライアントはスレッドセーフ）。

event引数（初回バックフィル等での明示指定用。省略時は環境変数のデフォルト値を使う）:
  {"since": "2026-03-07T00:00:00Z", "until": "2026-03-14T00:00:00Z", "max_partitions": 200}
"""

import json
import os
import time
from concurrent.futures import ThreadPoolExecutor
from datetime import datetime, timezone, timedelta

import boto3

s3 = boto3.client("s3")

S3_BUCKET = os.environ["S3_BUCKET"]
ARCHIVE_BUCKET = os.environ["ARCHIVE_BUCKET"]
LOOKBACK_HOURS_DEFAULT = int(os.environ.get("LOOKBACK_HOURS", "72"))
MAX_PARTITIONS_PER_RUN_DEFAULT = int(os.environ.get("MAX_PARTITIONS_PER_RUN", "200"))

MERGED_NAME = "_merged.json"
_MAX_WORKERS = 12
_COPY_RETRY = 3


def _parse_iso(s: str) -> datetime:
    return datetime.strptime(s, "%Y-%m-%dT%H:%M:%SZ").replace(tzinfo=timezone.utc)


def _prefix_for(dt: datetime) -> str:
    return (
        f"raw/year={dt.strftime('%Y')}/month={dt.strftime('%m')}/"
        f"day={dt.strftime('%d')}/hour={dt.strftime('%H')}/"
    )


def _target_hour_partitions(since: datetime, until: datetime, max_partitions: int) -> list:
    """[since, until) の範囲を1時間刻みで新しい方から列挙する。UTC基準。"""
    partitions = []
    cur = until
    while cur > since and len(partitions) < max_partitions:
        cur -= timedelta(hours=1)
        partitions.append(cur)
    return partitions


def _list_partition(prefix: str) -> list:
    keys = []
    paginator = s3.get_paginator("list_objects_v2")
    for page in paginator.paginate(Bucket=S3_BUCKET, Prefix=prefix):
        for obj in page.get("Contents", []):
            keys.append(obj["Key"])
    return keys


def _get_lines(key: str) -> list:
    """1オブジェクトを読み、NDJSON行のリストとして返す（不正な行はスキップ）。
    内容は妥当性チェックのみに使い、書き戻す文字列は元のバイト列そのまま
    （再シリアライズによる浮動小数点・キー順の変化を避けるため）。"""
    body = s3.get_object(Bucket=S3_BUCKET, Key=key)["Body"].read()
    lines = []
    for raw_line in body.splitlines():
        line = raw_line.strip()
        if not line:
            continue
        try:
            json.loads(line)
        except Exception as e:
            print(f"[WARN] invalid json, skip: {key}: {e}")
            continue
        lines.append(line.decode("utf-8"))
    return lines


def _archive_and_delete(key: str) -> bool:
    """1オブジェクトをARCHIVE_BUCKETへ退避してから削除する。失敗時はリトライする。"""
    for attempt in range(_COPY_RETRY):
        try:
            s3.copy_object(
                Bucket=ARCHIVE_BUCKET,
                CopySource={"Bucket": S3_BUCKET, "Key": key},
                Key=key,
            )
            s3.delete_object(Bucket=S3_BUCKET, Key=key)
            return True
        except Exception as e:
            print(f"[WARN] archive/delete failed (attempt {attempt + 1}/{_COPY_RETRY}) for {key}: {e}")
            time.sleep(0.5 * (attempt + 1))
    print(f"[ERROR] giving up archiving {key}, left in place for next run")
    return False


def _compact_partition(dt: datetime) -> dict:
    prefix = _prefix_for(dt)
    keys = _list_partition(prefix)
    merged_key = prefix + MERGED_NAME
    stragglers = [k for k in keys if k != merged_key]
    has_merged = merged_key in keys

    if not stragglers:
        return {"partition": prefix, "action": "skip", "archived": 0}
    if not has_merged and len(stragglers) < 2:
        return {"partition": prefix, "action": "skip", "archived": 0}

    with ThreadPoolExecutor(max_workers=_MAX_WORKERS) as pool:
        new_lines_per_file = list(pool.map(_get_lines, stragglers))
    new_lines = [line for lines in new_lines_per_file for line in lines]

    existing_lines = _get_lines(merged_key) if has_merged else []
    all_lines = existing_lines + new_lines
    if not all_lines:
        print(f"[SKIP] no valid records to merge in {prefix}")
        return {"partition": prefix, "action": "skip", "archived": 0}

    s3.put_object(
        Bucket=S3_BUCKET,
        Key=merged_key,
        Body=("\n".join(all_lines) + "\n").encode("utf-8"),
        ContentType="application/json",
    )
    print(f"[OK] merged {len(stragglers)} files ({len(new_lines)} records) -> s3://{S3_BUCKET}/{merged_key}")

    with ThreadPoolExecutor(max_workers=_MAX_WORKERS) as pool:
        archived_flags = list(pool.map(_archive_and_delete, stragglers))
    archived = sum(1 for ok in archived_flags if ok)
    print(f"[OK] archived {archived}/{len(stragglers)} original files for {prefix}")
    return {"partition": prefix, "action": "merged", "archived": archived}


def handler(event, context):
    event = event or {}
    now = datetime.now(timezone.utc)

    lookback_hours = int(event.get("lookback_hours", LOOKBACK_HOURS_DEFAULT))
    max_partitions = int(event.get("max_partitions", MAX_PARTITIONS_PER_RUN_DEFAULT))

    # 現在進行中のhour + 直前1hourを安全マージン（ingest Lambdaとのレース回避）として除外する
    default_until = now.replace(minute=0, second=0, microsecond=0) - timedelta(hours=1)
    until = _parse_iso(event["until"]) if event.get("until") else default_until
    since = _parse_iso(event["since"]) if event.get("since") else until - timedelta(hours=lookback_hours)

    partitions = _target_hour_partitions(since, until, max_partitions)
    print(f"[OK] compaction run: {len(partitions)} partitions from {since.isoformat()} to {until.isoformat()}")

    results = []
    total_archived = 0
    for dt in partitions:
        try:
            r = _compact_partition(dt)
        except Exception as e:
            print(f"[ERROR] compaction failed for {_prefix_for(dt)}: {e}")
            r = {"partition": _prefix_for(dt), "action": "error", "archived": 0}
        results.append(r)
        total_archived += r["archived"]

    merged_count = sum(1 for r in results if r["action"] == "merged")
    print(f"[OK] compaction run complete: partitions={len(partitions)} merged={merged_count} archived={total_archived}")
    return {
        "partitions_scanned": len(partitions),
        "partitions_merged": merged_count,
        "files_archived": total_archived,
    }
