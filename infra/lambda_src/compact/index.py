"""
S3 raw データの定期 compaction

ingest Lambda（lambda_src/ingest/index.py）が raw/year=.../hour=.../{device_id}-{uuid8}.json
にメッセージ1件1オブジェクトで書き込むため、Grafana/Athenaが高頻度（約15分おき）で開く
S3ファイル数が多くなりすぎている（S3リクエスト課金の主因。esp32_iot_gateway/CONTEXT.md参照）。
EventBridge Scheduler により毎時実行し（UTC評価）、確定済みhourパーティション内の小ファイルを
1つのNDJSONファイル `merged.json` にまとめる。

冪等性: `merged.json` 以外のファイル（straggler）が無ければ何もしない。既存の`merged.json`
があれば吸収して上書き、無ければstraggler2件以上の時だけ新規作成する（1件のみはスキップ）。
パーティション値はS3パス由来（Glue partition projection）のため、hour境界をまたぐマージは
しない。

マージ元はARCHIVE_BUCKETへ個別コピーしてからまとめて削除する（コピー失敗分は次回に持ち越し、
重複行が発生しうるがデータ消失よりマシとして許容）。並列化・バッチ分割の設計判断は各定数・
関数のコメント参照。

event引数（バックフィル用。省略時は環境変数のデフォルト値）:
  {"since": "2026-03-07T00:00:00Z", "until": "2026-03-14T00:00:00Z", "max_partitions": 200}
"""

import json
import os
import time
from concurrent.futures import ThreadPoolExecutor
from datetime import datetime, timezone, timedelta

import boto3
from botocore.config import Config

_WORKERS = 32  # Get/Copyの並列数（1バッチ分をまとめて処理する）
_BATCH_SIZE = 20  # 1バッチあたりのパーティション数（タイムアウト時の被害範囲をここに抑える）
_COPY_RETRY = 3

# ThreadPoolExecutorの並列数(_WORKERS)がboto3のデフォルト接続プール(10)を
# 上回ると "Connection pool is full" が頻発し、接続の作り直しで遅くなるため広げておく
s3 = boto3.client("s3", config=Config(max_pool_connections=_WORKERS + 8))

S3_BUCKET = os.environ["S3_BUCKET"]
ARCHIVE_BUCKET = os.environ["ARCHIVE_BUCKET"]
LOOKBACK_HOURS_DEFAULT = int(os.environ.get("LOOKBACK_HOURS", "72"))
MAX_PARTITIONS_PER_RUN_DEFAULT = int(os.environ.get("MAX_PARTITIONS_PER_RUN", "200"))

MERGED_NAME = "merged.json"  # 先頭 "_"/"." はHadoop系InputFormatが隠しファイルとして無視するため不可


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


def _plan_partition(dt: datetime):
    """パーティションを調べ、圧縮対象かどうかを判定する。対象なら
    {prefix, merged_key, stragglers, has_merged} を返す。対象外なら None。"""
    prefix = _prefix_for(dt)
    keys = _list_partition(prefix)
    merged_key = prefix + MERGED_NAME
    stragglers = [k for k in keys if k != merged_key]
    has_merged = merged_key in keys

    if not stragglers:
        return None
    if not has_merged and len(stragglers) < 2:
        return None
    return {
        "prefix": prefix,
        "merged_key": merged_key,
        "stragglers": stragglers,
        "has_merged": has_merged,
    }


def _get_lines(key: str) -> list:
    """1オブジェクトを読み、NDJSON行のリストとして返す（不正な行・読み取り失敗はスキップ）。
    内容は妥当性チェックのみに使い、書き戻す文字列は元のバイト列そのまま
    （再シリアライズによる浮動小数点・キー順の変化を避けるため）。
    1件の読み取り失敗で他のファイルを巻き込まないよう、ここで例外を吸収する
    （failed keyはstragglerとしてraw/に残り続けるので、次回実行時に自然にリトライされる）。"""
    try:
        body = s3.get_object(Bucket=S3_BUCKET, Key=key)["Body"].read()
    except Exception as e:
        print(f"[WARN] failed to read {key}, skip (will retry next run): {e}")
        return []
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


def _copy_to_archive(key: str) -> bool:
    """1オブジェクトをARCHIVE_BUCKETへコピーする（削除はしない）。失敗時はリトライする。"""
    for attempt in range(_COPY_RETRY):
        try:
            s3.copy_object(
                Bucket=ARCHIVE_BUCKET,
                CopySource={"Bucket": S3_BUCKET, "Key": key},
                Key=key,
            )
            return True
        except Exception as e:
            print(f"[WARN] archive copy failed (attempt {attempt + 1}/{_COPY_RETRY}) for {key}: {e}")
            time.sleep(0.5 * (attempt + 1))
    print(f"[ERROR] giving up archiving {key}, left in place for next run")
    return False


def _batch_delete(keys: list) -> int:
    """delete_objects（最大1000件/回）でまとめて削除する。成功した件数を返す。"""
    deleted = 0
    for i in range(0, len(keys), 1000):
        chunk = keys[i:i + 1000]
        try:
            resp = s3.delete_objects(
                Bucket=S3_BUCKET,
                Delete={"Objects": [{"Key": k} for k in chunk], "Quiet": True},
            )
            errors = resp.get("Errors", [])
            deleted += len(chunk) - len(errors)
            for err in errors:
                print(f"[WARN] batch delete failed for {err.get('Key')}: {err.get('Message')}")
        except Exception as e:
            print(f"[ERROR] batch delete request failed for {len(chunk)} keys: {e}")
    return deleted


def _process_batch(batch_plans: list) -> dict:
    """plansのうち1バッチ分（最大BATCH_SIZE件）に対してGet→merge書き込み→
    archive(Copy)→削除(まとめてdelete_objects)を実行する。バッチ内で広く並列化する。"""

    # ---- Step 2: このバッチ分のGetObjectを1つの広いプールでまとめて並列実行 ----
    read_targets = []  # (plan_index, key, is_existing_merged)
    for i, p in enumerate(batch_plans):
        if p["has_merged"]:
            read_targets.append((i, p["merged_key"], True))
        for key in p["stragglers"]:
            read_targets.append((i, key, False))

    with ThreadPoolExecutor(max_workers=_WORKERS) as pool:
        read_results = list(pool.map(lambda t: (t[0], t[2], _get_lines(t[1])), read_targets))

    existing_lines = [[] for _ in batch_plans]
    new_lines = [[] for _ in batch_plans]
    for i, is_existing, lines in read_results:
        (existing_lines if is_existing else new_lines)[i].extend(lines)

    # ---- Step 3: パーティションごとにmerged.jsonを書き込む ----
    merged_count = 0
    failed = set()
    for i, p in enumerate(batch_plans):
        all_lines = existing_lines[i] + new_lines[i]
        if not all_lines:
            # 全stragglerが「有効な行0件」（不正JSON・読み取り失敗等）。マージすべき
            # 中身はないが、raw/に残しておく理由もないのでarchiveだけは行う
            # （放置すると毎回再検出されては読めずスキップされ続け、永久にraw/に残ってしまう）
            print(f"[SKIP] no valid records to merge in {p['prefix']}, archiving {len(p['stragglers'])} unusable file(s)")
            continue
        try:
            s3.put_object(
                Bucket=S3_BUCKET,
                Key=p["merged_key"],
                Body=("\n".join(all_lines) + "\n").encode("utf-8"),
                ContentType="application/json",
            )
            merged_count += 1
            print(f"[OK] merged {len(p['stragglers'])} files ({len(new_lines[i])} new records) -> s3://{S3_BUCKET}/{p['merged_key']}")
        except Exception as e:
            print(f"[ERROR] failed to write merged.json for {p['prefix']}: {e}")
            failed.add(i)  # マージ失敗パーティションはarchiveせず次回に持ち越す

    # ---- Step 4: このバッチ分のarchive(CopyObject)を1つの広いプールでまとめて並列実行 ----
    copy_targets = [(i, key) for i, p in enumerate(batch_plans) if i not in failed for key in p["stragglers"]]
    with ThreadPoolExecutor(max_workers=_WORKERS) as pool:
        copy_results = list(pool.map(lambda t: (t[0], t[1], _copy_to_archive(t[1])), copy_targets))

    copied_ok = [[] for _ in batch_plans]
    for i, key, ok in copy_results:
        if ok:
            copied_ok[i].append(key)

    # ---- Step 5: パーティションごとにdelete_objectsでまとめて削除 ----
    total_archived = 0
    for i, p in enumerate(batch_plans):
        keys_to_delete = copied_ok[i]
        if not keys_to_delete:
            continue
        deleted = _batch_delete(keys_to_delete)
        total_archived += deleted
        print(f"[OK] archived {deleted}/{len(p['stragglers'])} original files for {p['prefix']}")

    return {"merged": merged_count, "archived": total_archived}


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
    actual_since = partitions[-1] if partitions else until
    if actual_since > since:
        # max_partitions で打ち切られ、要求された since まで到達しなかった
        print(
            f"[OK] compaction run: {len(partitions)} partitions from {actual_since.isoformat()} to {until.isoformat()} "
            f"(truncated by max_partitions={max_partitions}; requested since={since.isoformat()})"
        )
    else:
        print(f"[OK] compaction run: {len(partitions)} partitions from {actual_since.isoformat()} to {until.isoformat()}")

    # ---- Step 1: 対象パーティションを判定（Listは軽量なので直列で十分） ----
    plans = []
    for dt in partitions:
        try:
            p = _plan_partition(dt)
        except Exception as e:
            print(f"[ERROR] failed to list partition {_prefix_for(dt)}: {e}")
            continue
        if p:
            plans.append(p)

    if not plans:
        print(f"[OK] compaction run complete: partitions={len(partitions)} merged=0 archived=0")
        return {"partitions_scanned": len(partitions), "partitions_merged": 0, "files_archived": 0}

    # BATCH_SIZE件ずつに区切って処理する（タイムアウト時の被害範囲を1バッチ分に限定するため。
    # モジュールdocstring・_BATCH_SIZEのコメント参照）
    merged_count = 0
    total_archived = 0
    for start in range(0, len(plans), _BATCH_SIZE):
        batch = plans[start:start + _BATCH_SIZE]
        try:
            result = _process_batch(batch)
            merged_count += result["merged"]
            total_archived += result["archived"]
        except Exception as e:
            print(f"[ERROR] batch processing failed (partitions {start}-{start + len(batch) - 1}): {e}")

    print(f"[OK] compaction run complete: partitions={len(partitions)} merged={merged_count} archived={total_archived}")
    return {
        "partitions_scanned": len(partitions),
        "partitions_merged": merged_count,
        "files_archived": total_archived,
    }
