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

os.environ["S3_BUCKET"] = "test-main-bucket"
os.environ["WATERMARK_TABLE"] = "test-device-watermark"

_INDEX_PATH = os.path.join(os.path.dirname(__file__), "..", "index.py")


@pytest.fixture
def obd_ingest():
    """S3・DynamoDBをmoto差し替えした状態でobd_ingest/index.pyを毎回新しいモジュールとして
    ロードして返す。spec_from_file_locationで一意な名前でロードする理由はingest/compact等の
    conftestと同じ（同一pytestプロセス内でのsys.modules名前衝突を避けるため）。"""
    with mock_aws():
        s3 = boto3.client("s3")
        s3.create_bucket(Bucket=os.environ["S3_BUCKET"])

        dynamodb = boto3.client("dynamodb")
        dynamodb.create_table(
            TableName=os.environ["WATERMARK_TABLE"],
            AttributeDefinitions=[{"AttributeName": "device_id", "AttributeType": "S"}],
            KeySchema=[{"AttributeName": "device_id", "KeyType": "HASH"}],
            BillingMode="PAY_PER_REQUEST",
        )

        spec = importlib.util.spec_from_file_location("obd_ingest_index", _INDEX_PATH)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        yield module
