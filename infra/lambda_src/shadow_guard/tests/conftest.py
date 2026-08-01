import importlib.util
import os

import pytest
from moto import mock_aws

os.environ["AWS_ACCESS_KEY_ID"] = "testing"
os.environ["AWS_SECRET_ACCESS_KEY"] = "testing"
os.environ["AWS_SECURITY_TOKEN"] = "testing"
os.environ["AWS_SESSION_TOKEN"] = "testing"
os.environ["AWS_DEFAULT_REGION"] = "us-east-1"

os.environ["IOT_ENDPOINT"] = "https://example-iot-endpoint.iot.us-east-1.amazonaws.com"

_INDEX_PATH = os.path.join(os.path.dirname(__file__), "..", "index.py")


@pytest.fixture
def shadow_guard():
    """shadow_guard/index.pyを毎回新しいモジュールとしてロードして返す。
    moto の iot-data は update_thing_shadow を実装していないため、mock_aws()は
    「テスト中に本物のAWSへ通信が漏れるのを防ぐ安全網」として使うのみで、
    実際のupdate_thing_shadow呼び出し確認は各テストでmonkeypatchする。"""
    with mock_aws():
        spec = importlib.util.spec_from_file_location("shadow_guard_index", _INDEX_PATH)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        yield module
