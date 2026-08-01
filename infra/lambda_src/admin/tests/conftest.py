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
os.environ["ACCOUNT_ID"] = "123456789012"

_INDEX_PATH = os.path.join(os.path.dirname(__file__), "..", "index.py")


@pytest.fixture
def admin():
    """IoT/IoT Dataをmoto差し替えした状態でadmin/index.pyを毎回新しいモジュールとしてロードして返す。"""
    with mock_aws():
        spec = importlib.util.spec_from_file_location("admin_index", _INDEX_PATH)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        yield module
