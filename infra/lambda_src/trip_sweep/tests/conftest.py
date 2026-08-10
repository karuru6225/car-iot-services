import importlib.util
import os

import boto3
import pytest
from moto import mock_aws

os.environ["AWS_ACCESS_KEY_ID"] = "testing"
os.environ["AWS_SECRET_ACCESS_KEY"] = "testing"
os.environ["AWS_SECURITY_TOKEN"] = "testing"
os.environ["AWS_SESSION_TOKEN"] = "testing"
os.environ["AWS_DEFAULT_REGION"] = "us-east-1"

os.environ["WATERMARK_TABLE"] = "test-device-watermark"
os.environ["TRIP_SUMMARY_TABLE"] = "test-trip-summary"
os.environ["ATHENA_DATABASE"] = "test_db"
os.environ["ATHENA_WORKGROUP"] = "test-workgroup"

_INDEX_PATH = os.path.join(os.path.dirname(__file__), "..", "index.py")


@pytest.fixture
def trip_sweep():
    """DynamoDBをmoto差し替えした状態でtrip_sweep/index.pyを毎回新しいモジュールとして
    ロードして返す。Athenaは実クエリを実行できない（motoはSQL実行までは面倒を見ない）ため、
    Athena依存の関数を使うテストは個別にmonkeypatchする（query/index.pyのconftestと同じ方針）。"""
    with mock_aws():
        dynamodb = boto3.client("dynamodb")
        dynamodb.create_table(
            TableName=os.environ["WATERMARK_TABLE"],
            AttributeDefinitions=[
                {"AttributeName": "device_id", "AttributeType": "S"},
                {"AttributeName": "open_marker", "AttributeType": "S"},
            ],
            KeySchema=[{"AttributeName": "device_id", "KeyType": "HASH"}],
            GlobalSecondaryIndexes=[
                {
                    "IndexName": "open-index",
                    "KeySchema": [{"AttributeName": "open_marker", "KeyType": "HASH"}],
                    "Projection": {"ProjectionType": "ALL"},
                }
            ],
            BillingMode="PAY_PER_REQUEST",
        )
        dynamodb.create_table(
            TableName=os.environ["TRIP_SUMMARY_TABLE"],
            AttributeDefinitions=[
                {"AttributeName": "device_id", "AttributeType": "S"},
                {"AttributeName": "session_start", "AttributeType": "N"},
            ],
            KeySchema=[
                {"AttributeName": "device_id", "KeyType": "HASH"},
                {"AttributeName": "session_start", "KeyType": "RANGE"},
            ],
            BillingMode="PAY_PER_REQUEST",
        )

        spec = importlib.util.spec_from_file_location("trip_sweep_index", _INDEX_PATH)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        yield module
