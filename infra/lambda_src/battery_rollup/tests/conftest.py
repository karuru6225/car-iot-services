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

os.environ["S3_BUCKET"] = "test-bucket"
os.environ["ATHENA_DATABASE"] = "test_db"
os.environ["ATHENA_WORKGROUP"] = "test-workgroup"
os.environ["REPROCESS_LOOKBACK_DAYS"] = "3"
os.environ["MAX_DAYS_PER_RUN"] = "30"
os.environ["ATHENA_POLL_TIMEOUT_SEC"] = "600"

_INDEX_PATH = os.path.join(os.path.dirname(__file__), "..", "index.py")


@pytest.fixture
def battery_rollup():
    """S3をmoto差し替えした状態でbattery_rollup/index.pyを毎回新しいモジュールとして
    ロードして返す。Athenaは実クエリを実行できない（motoはSQL実行までは面倒を見ない）ため、
    _run_athena_query依存のテストは個別にmonkeypatchする（trip_analysisのconftestと同じ方針）。"""
    with mock_aws():
        s3 = boto3.client("s3")
        s3.create_bucket(Bucket=os.environ["S3_BUCKET"])

        spec = importlib.util.spec_from_file_location("battery_rollup_index", _INDEX_PATH)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        yield module
