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
os.environ["THING_NAME"] = "esp32-gw-abc123"

_INDEX_PATH = os.path.join(os.path.dirname(__file__), "..", "index.py")


@pytest.fixture
def status():
    """status/index.pyを毎回新しいモジュールとしてロードして返す。
    moto の iot-data は get_thing_shadow を実装しきれていないため、各テストで
    get_thing_shadowをmonkeypatchする（mock_aws()は本物のAWSに漏れないための安全網）。"""
    with mock_aws():
        spec = importlib.util.spec_from_file_location("status_index", _INDEX_PATH)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        yield module
