import importlib
import os
import sys

import boto3
import pytest
from moto import mock_aws

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

os.environ["AWS_ACCESS_KEY_ID"] = "testing"
os.environ["AWS_SECRET_ACCESS_KEY"] = "testing"
os.environ["AWS_SECURITY_TOKEN"] = "testing"
os.environ["AWS_SESSION_TOKEN"] = "testing"
os.environ["AWS_DEFAULT_REGION"] = "us-east-1"

os.environ["S3_BUCKET"] = "test-raw-bucket"
os.environ["ARCHIVE_BUCKET"] = "test-archive-bucket"


@pytest.fixture
def compact():
    """S3をmoto差し替えした状態でcompact/index.pyをロードし直して返す。
    s3クライアントはモジュールロード時に生成されるため、mock_aws()の中でreloadする。"""
    with mock_aws():
        s3 = boto3.client("s3")
        s3.create_bucket(Bucket=os.environ["S3_BUCKET"])
        s3.create_bucket(Bucket=os.environ["ARCHIVE_BUCKET"])

        import index as compact_index

        importlib.reload(compact_index)
        yield compact_index
