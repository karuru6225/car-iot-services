import importlib.util
import os
import sys

import pytest

_LAMBDA_DIR = os.path.join(os.path.dirname(__file__), "..")
sys.path.insert(0, _LAMBDA_DIR)  # carmcp パッケージ
sys.path.insert(0, os.path.dirname(__file__))  # car_mcp_fakes

from car_mcp_fakes import FakeGateway  # noqa: E402
from carmcp.config import Config  # noqa: E402


@pytest.fixture
def cfg() -> Config:
    return Config(
        device_id="esp32-gw-aabbccddeeff",
        obd_device_id="car-iot-aabbcc",
        s3_bucket="bucket",
        athena_database="iot_monitor",
        athena_workgroup="iot-monitor",
        iot_endpoint="https://example-ats.iot.ap-northeast-1.amazonaws.com",
        expose_location=False,
    )


@pytest.fixture
def car_mcp(monkeypatch):
    """index.pyを毎回新しいモジュールとしてロードし、AWSへの読み取りを偽物に差し替えて返す。
    index.pyはロード時に環境変数を読みboto3のクライアントを作るので、先に環境変数を入れる。"""
    for name, value in {
        "AWS_ACCESS_KEY_ID": "testing",
        "AWS_SECRET_ACCESS_KEY": "testing",
        "AWS_DEFAULT_REGION": "ap-northeast-1",
        "CAR_DEVICE_ID": "esp32-gw-aabbccddeeff",
        "S3_BUCKET": "bucket",
        "ATHENA_DATABASE": "iot_monitor",
        "ATHENA_WORKGROUP": "iot-monitor",
        "IOT_ENDPOINT": "https://example-ats.iot.ap-northeast-1.amazonaws.com",
    }.items():
        monkeypatch.setenv(name, value)
    spec = importlib.util.spec_from_file_location("car_mcp_index", os.path.join(_LAMBDA_DIR, "index.py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    monkeypatch.setattr(module, "gw", FakeGateway())
    return module
