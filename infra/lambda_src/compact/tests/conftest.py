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
os.environ["ARCHIVE_BUCKET"] = "test-archive-bucket"

_INDEX_PATH = os.path.join(os.path.dirname(__file__), "..", "index.py")


@pytest.fixture
def compact():
    """S3をmoto差し替えした状態でcompact/index.pyを毎回新しいモジュールとしてロードして返す。
    複数Lambda分のテストが同一pytestプロセス内で動くため、`import index`のような
    共通モジュール名は使わずspec_from_file_locationで一意にロードする
    （sys.modulesでの名前衝突を避けるため）。s3クライアントはモジュールロード時に
    生成されるため、mock_aws()の中でロードする。"""
    with mock_aws():
        s3 = boto3.client("s3")
        s3.create_bucket(Bucket=os.environ["S3_BUCKET"])
        s3.create_bucket(Bucket=os.environ["ARCHIVE_BUCKET"])

        spec = importlib.util.spec_from_file_location("compact_index", _INDEX_PATH)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        yield module
