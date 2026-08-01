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

os.environ["S3_BUCKET"] = "test-raw-bucket"
os.environ["CORRUPTED_BUCKET"] = "test-corrupted-bucket"

_INDEX_PATH = os.path.join(os.path.dirname(__file__), "..", "index.py")


@pytest.fixture
def ingest():
    """S3をmoto差し替えした状態でingest/index.pyを毎回新しいモジュールとしてロードして返す。
    spec_from_file_locationで一意な名前でロードする理由はcompact/deleteのconftestと同じ
    （同一pytestプロセス内でのsys.modules名前衝突を避けるため）。"""
    with mock_aws():
        s3 = boto3.client("s3")
        s3.create_bucket(Bucket=os.environ["S3_BUCKET"])
        s3.create_bucket(Bucket=os.environ["CORRUPTED_BUCKET"])

        spec = importlib.util.spec_from_file_location("ingest_index", _INDEX_PATH)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        yield module
