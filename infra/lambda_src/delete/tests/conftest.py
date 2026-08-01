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
os.environ["ATHENA_DATABASE"] = "test_db"
os.environ["ATHENA_WORKGROUP"] = "test-workgroup"

_INDEX_PATH = os.path.join(os.path.dirname(__file__), "..", "index.py")


@pytest.fixture
def delete():
    """S3をmoto差し替えした状態でdelete/index.pyを毎回新しいモジュールとしてロードして返す。
    複数Lambda分のテストが同一pytestプロセス内で動くため、`import index`のような
    共通モジュール名は使わずspec_from_file_locationで一意にロードする
    （sys.modulesでの名前衝突を避けるため）。
    Athenaは実クエリを実行できない（motoはSQL実行までは面倒を見ない）ため、
    Athena依存の関数を使うテストは個別にmonkeypatchする。"""
    with mock_aws():
        s3 = boto3.client("s3")
        s3.create_bucket(Bucket=os.environ["S3_BUCKET"])

        spec = importlib.util.spec_from_file_location("delete_index", _INDEX_PATH)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        yield module
