"""
GET  /labels  → ユーザーのラベル設定を返す
PUT  /labels  → ユーザーのラベル設定を保存する

ラベルは S3 の labels/{cognito-sub}.json に保存。
sub は JWT claims から取得するため、他ユーザーのデータは操作不可。
"""

import json
import os

import boto3
from botocore.exceptions import ClientError

s3        = boto3.client("s3")
S3_BUCKET = os.environ["S3_BUCKET"]


def _sub(event: dict) -> str:
    return (
        event.get("requestContext", {})
             .get("authorizer", {})
             .get("jwt", {})
             .get("claims", {})
             .get("sub", "")
    )


def handler(event, context):
    sub = _sub(event)
    if not sub:
        return {
            "statusCode": 401,
            "headers": {"Content-Type": "application/json"},
            "body": json.dumps({"error": "Unauthorized"}),
        }

    key    = f"labels/{sub}.json"
    method = event.get("requestContext", {}).get("http", {}).get("method", "")

    if method == "GET":
        try:
            obj = s3.get_object(Bucket=S3_BUCKET, Key=key)
            return {
                "statusCode": 200,
                "headers": {"Content-Type": "application/json"},
                "body": obj["Body"].read().decode(),
            }
        except ClientError as e:
            if e.response["Error"]["Code"] == "NoSuchKey":
                return {
                    "statusCode": 200,
                    "headers": {"Content-Type": "application/json"},
                    "body": json.dumps({}),
                }
            print(f"[ERROR] {e}")
            return {
                "statusCode": 500,
                "headers": {"Content-Type": "application/json"},
                "body": json.dumps({"error": str(e)}),
            }

    if method == "PUT":
        try:
            body = json.loads(event.get("body") or "{}")
        except json.JSONDecodeError:
            return {
                "statusCode": 400,
                "headers": {"Content-Type": "application/json"},
                "body": json.dumps({"error": "Invalid JSON"}),
            }

        try:
            s3.put_object(
                Bucket=S3_BUCKET,
                Key=key,
                Body=json.dumps(body),
                ContentType="application/json",
            )
            return {
                "statusCode": 200,
                "headers": {"Content-Type": "application/json"},
                "body": json.dumps({"ok": True}),
            }
        except Exception as e:
            print(f"[ERROR] {e}")
            return {
                "statusCode": 500,
                "headers": {"Content-Type": "application/json"},
                "body": json.dumps({"error": str(e)}),
            }

    return {
        "statusCode": 405,
        "headers": {"Content-Type": "application/json"},
        "body": json.dumps({"error": "Method Not Allowed"}),
    }
